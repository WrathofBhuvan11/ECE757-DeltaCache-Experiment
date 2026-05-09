# DeltaCache -- Design Evolution and Final Architecture

This document walks through how pairwise delta-compression at the LLC was
originally conceived for this project, why the original 6-transient-state
coherence design didn't hold up under multi-core stress tests, and what
the new architecture (5 transient states + minimum-sharer invariant +
system-functional-read recovery) does differently. It's organized as a
design narrative, not a changelog -- the per-file edit list lives at the
bottom.

---

## 1. The original goal

Compress the L3 (LLC) by pairing two cache lines whose contents are
"close" in a byte-wise sense, and storing only the **delta** between
them. If the deltas are small (most bytes near zero), they encode well
in any byte-granularity compressor; even without a downstream BDI/null
encoder, the *capacity gain* shows up the moment we collapse two
physical data slots into one.

Concretely:

```
Pair (A, B) at the L3:
  A's L3 entry: tag=A, isDeltad=true, deltaPair=B, deltaDirection=...
  B's L3 entry: tag=B, isDeltad=true, deltaPair=A, deltaDirection=...
  The single 64-byte payload they share = byte-wise (A_raw - B_raw)
  or (B_raw - A_raw), whichever has lower entropy.
```

**Direction bit per pair side**: rather than always storing `B - A`, the
compress step computes both `B - A` and `A - B`, picks whichever has the
smaller byte-sum (proxy for entropy), stores it once, and records on
each side whether it goes "I = partner + delta" (ADD) or "I = partner -
delta" (SUB) to reconstruct. That's the per-side `deltaDirection` bit.

**Hash-driven pairing**: a small map table keyed on a content hash
(`generateMapValue`) finds candidate partners -- first time a hash slot
is hit, store the address; on the next hit with a different address,
the two are candidate partners and `recordDelta` gets called.

The byte-wise math (compress and decompress) lives in
`DeltaMapTable.hh` -- `computeAndStoreDelta` and `undeltaPair` -- and
those have been correct since v0. The bugs were never in the algebra;
they were always in the **coherence protocol** that has to keep the
pair's invariants holding while the rest of the system is moving.

---

## 2. The original FSM -- 6 transient delta states

The first design extended a stock MESI-derived 3-level Ruby protocol by
adding six new transient states for "something is in flight while a
line is delta-paired":

| Old state    | Triggered by                                        | What it was waiting for                                            |
|--------------|-----------------------------------------------------|--------------------------------------------------------------------|
| `SS_DLT_GS`  | GETS on a delta-paired line in SS                   | Direct DLT_DATA from a peer L1 sharer of B (the requested side)   |
| `M_DLT_GS`   | GETS on a delta-paired line in M (no upper holders) | Cross-fetch: A's raw bytes from one of A's sharers                |
| `SS_DLT_GM`  | (legacy/unused, kept for reference)                 | --                                                                  |
| `M_DLT_GM`   | GETM on a delta-paired line in M                    | Cross-fetch from partner's sharer + Exclusive_Unblock from caller |
| `SS_DLT_GM_X`| GETM on a delta-paired line in SS from a non-sharer | INV-acks from the other sharers + DLT_DATA from the chosen one    |
| `SS_I_DLT`   | L3 evicting a delta-paired SS victim                | INV-acks + DLT_DATA from a sharer                                  |
| `M_I_DLT`    | L3 evicting a delta-paired M victim                 | DLT_DATA cross-fetch from partner's sharer                         |

The dispatch logic in `responseL2Network_in` and `L1RequestL2Network_in`
in_ports classified incoming requests by `(cache_entry.CacheState,
cache_entry.isDeltad, requestor relationship to Sharers, ...)` and
fired the matching event to enter one of those states. Each state had:

- An entry transition that issued the right cross-fetch / INV pattern.
- A set of "completion" events (`Dlt_GS_Data_Same`, `Ack`, `Ack_all`,
  `Dlt_GM_X_Partial`, `Dlt_GM_X_Last`, `Dlt_Evict_Self_Last`, etc.)
  that combined arriving network responses with the parked TBE state.
- An exit transition that did the actual undelta (using the
  reconstruction action variants `sgss_`, `sgsx_`, `rdss_`, `crm_`,
  `cev_`, `udte_`, `adgmx_`, `uu_`).
- A stall list for any L1 request that hit during the transient window
  -- those stalled until the controller wokeUpDependents.

### Why this design was reasonable on paper

Each state corresponded to a specific *combination* of:

- **L3 base state** (SS = upper sharers exist; M = no upper sharers).
- **What we need to do next** (GETS-direct, GETS-cross, GETM-direct,
  GETM-cross, evict-with-sharers, evict-without-sharers).

So 2 × 3 + 1 = 7 conceptual states; collapsing the impossible
`MT_DLT_*` cases left the 6 transients above. Each state had a clean,
well-defined exit. The reconstruction actions (`sgsx_undeltaAndShipCrossToGetSReq`,
`adgmx_adoptAndUndeltaForGetmCross`, etc.) all called `undeltaPair` with
correct direction handling.

### Where it broke

Three classes of bug, all in the coherence layer:

#### 2.1 The 5c-random-tester deadlock at tick 84,700,681

`SS_DLT_GS` would issue `FWD_PARTNER_DLT` to a *specific* L1 sharer of B
(picked by `Sharers.smallestElement`) and park waiting for that sharer's
DLT_DATA response. If the chosen sharer had a GETM in flight at the
same time and its L1 transitioned `SS → I` between when L3 picked it
and when the FWD_PARTNER_DLT message landed in its queue, the L1 just
silently dropped the request. The L3 sat in `SS_DLT_GS` forever; every
subsequent L1 request on B's address stalled behind it; deadlock.

Various v7/v8/v9 attempts tried to add stale-Unblock detection, retry
on a different sharer, etc. None held up across all core counts.

#### 2.2 The 6c-random-tester panic on missing L1 transition

`(L1: IS, Fwd_GETM_Dlt)` had no transition in `L1cache.sm`. When the L1
was in IS (waiting for its own data after a GETS miss) and L3 sent a
forwarded GETM-delta-helper, the L1 panicked. v10 added the
`{I, IS, IS_I, IM} × Fwd_GETM_Dlt → fi_sendInvAck + l_popL2RequestQueue`
transition, which closed it.

#### 2.3 The daxpy SE-mode silent corruption at tick 177,022 (v0..v11)

This one took the longest to diagnose. The pattern:

1. At simulation tick 0, the SE-mode binary loader uses gem5's
   functional-write path to populate memory with the binary.
2. Some of those functional writes land on L3 entries that have already
   been delta-paired (because an earlier read populated L3 first).
3. The SLICC `functionalWrite` (from v12 onward) detected the paired
   entry and "dissolved" the pair: cleared `isDeltad` on both sides,
   then ran `testAndWrite` which overwrote the *current* side's
   `DataBlk` with the packet's new raw bytes.
4. **But the partner's `DataBlk` was left untouched** -- it still held
   delta bytes, but with `isDeltad=false`.
5. Some ticks later, the CPU demand-fetched the partner's address. The
   protocol routed it through the regular `(SS, L1_GETS) →
   ds_sendSharedDataToRequestor` handler because `isDeltad=false`. That
   action ships `cache_entry.DataBlk` blindly. L1 received delta bytes
   and stored them as raw.
6. CPU fetched instructions from L1 (now delta bytes), x86 decoder
   produced a bogus instruction (`MOV_P_R : st r12, DS:[t0 + t7 + 0x4df2f736]`),
   the address was unmapped, page-fault.

This was the most expensive bug to find because the protocol view of
events looked correct -- the pair *was* dissolved, the dispatch *was*
choosing the right handler. The bug was that the partner's L3 storage
was orphaned with stale delta bytes flagged as raw.

---

## 3. The breakthrough -- what each invariant must enforce

The old design failed because it tried to handle "pair partially live,
partially in-flight" with state-machine surgery. The new design steps
back and identifies the **invariants** that *must* hold for delta
compression to be safe, then enforces each one structurally.

### Invariant A: minimum-sharer invariant

> For any live pair (A, B), at least one of the two lines must have an
> external raw source -- either ≥1 upper L1 sharer, or memory holds a
> reconstructable copy.

This is the same property the XOR Cache paper calls out. If neither
side has any upper sharer and neither's memory copy is current,
reconstruction is impossible -- the L3 alone holds only the delta, and
the delta-only is information-theoretically half a line.

Consequence: `recordDelta` must refuse to pair when the new pair would
create the "no anchor" state. The original predicate already blocked
`cache_entry.CacheState == State:IM` (heading to MT_MB with one L1
holder about to modify silently). v15 added `cache_entry.CacheState ==
State:MT` because the `(MT, L1_PUTM, M)` writeback path was creating
pairs whose own side ended in M with **zero** sharers -- a direct
GETS/GETM hit on M then shipped delta bytes via the regular handler
(no Dlt_* transient triggered).

### Invariant B: state-transient consistency on Unblock

When the unified `DLT_GS` state (see §4 below) receives an `Unblock`
response, the right thing to do depends on whether the pair-side that
parked here was the SS-derived or M-derived flow. The dispatch can tell
these apart by looking at `cache_entry.Sharers.count()` at trigger
time:

- **Sharers > 0** ⇒ SS-derived. Legitimate completion is the
  Dlt_GS_Data_Same path (peer ships raw B → `S_MB`). Any Unblock that
  arrives here is stale.
- **Sharers == 0** ⇒ M-derived. Legitimate completion is the Unblock
  from the GetS requestor; that requestor becomes the first sharer and
  the line transitions to SS.

Crucially, the "sharer count" is enough information to disambiguate at
trigger time -- no per-state field, no extra event. That's what makes
the merge possible.

### Invariant C: functional access must reach outside the L3

For functional reads/writes (gem5's "side-channel" path used by the SE
binary loader, syscall data movement, and snooping), the L3 alone
cannot reconstruct either side of a pair. The partner's raw bytes have
to be pulled from some other place in the system (L1, L2, memory). The
v17 fix calls `RubySystem::functionalRead(pkt)` from inside the SLICC
`functionalWrite` to do exactly that.

### Invariant D: pair lifecycle must be matched

Every successful `recordDelta` (call it +1) must eventually be paired
with exactly one `decPairCount` call (call it −1) at a dissolve site.
Skip a dissolve and the live-pair count drifts upward forever; double
a dissolve and it goes negative or under-reports compression.

In the v15+v16+v17 code, dissolves fire at exactly eight places:
`functionalWrite`, `uu_undeltaIfPartnerDormant`,
`rdss_undeltaInlineFromGETM`, `crm_crossUndeltaForMGetm`,
`udcur_undeltaWithReceivedAsCurrent` (legacy/reference),
`udte_undeltaPartnerFromEvictionTBE`, `cev_crossUndeltaCompleteEvict`,
`adgmx_adoptAndUndeltaForGetmCross`. The cross-fetch path
`sgsx_undeltaAndShipCrossToGetSReq` is *not* counted because it
reconstructs into a TBE scratch -- the L3 pair stays compressed.

---

## 4. The new FSM -- 5 transient delta states

```
   [ NP ] -- L1_GETS / L1_GETM / Mem_Data --> ... --> [ SS or M ]
                                                         |
   recordDelta success ------> isDeltad=true on this and partner
                                                         |
                                                         v
                              +--------------------------+
   pair-dispatch entry         |                          |
   (sharer-count gated)        v                          v
   +--- L1_GETS_Dlt_Direct ---> [ DLT_GS ]            [ DLT_GM* ]
   |                            (unified GS state)    (split: see below)
   |
   +--- L1_GETS_Dlt_Cross ----> [ DLT_GS ]
   |
   +--- L1_GETM_Dlt_SS_X -----> [ SS_DLT_GM_X ]
   |
   +--- L1_GETM_Dlt_M --------> [ M_DLT_GM ]
   |
   +--- L2_Replacement_Delta -> [ SS_I_DLT ] or [ M_I_DLT ]
                                (split because eviction has different
                                 exit semantics: SS_I_DLT -> M_I when
                                 the chosen sharer's data lands; M_I_DLT
                                 just waits for the cross-fetch)
```

The five delta transients and their roles:

| State          | Entry from         | Waits for                                            | Exit                          |
|----------------|--------------------|------------------------------------------------------|-------------------------------|
| `DLT_GS`       | SS or M + GETS     | DLT_DATA (direct) or Unblock (cross)                 | `S_MB` or `SS`                |
| `M_DLT_GM`     | M + GETM           | Cross-fetch DLT_DATA + Exclusive_Unblock from caller | `MT`                          |
| `SS_DLT_GM_X`  | SS + GETM-by-non-sharer | INV-acks + DLT_DATA from chosen sharer          | `SS_MB` or pivot to `M_DLT_GM`|
| `SS_I_DLT`     | SS L3-eviction     | INV-acks + DLT_DATA from chosen sharer               | `M_I` (then `NP` on Mem_Ack)  |
| `M_I_DLT`      | M L3-eviction      | DLT_DATA cross-fetch from partner's sharer           | `NP` on Mem_Ack               |

The two relevant simplifications relative to the old design:

1. **`SS_DLT_GS` and `M_DLT_GS` collapsed into `DLT_GS`** (v16). The
   dispatch in the response in_port uses
   `cache_entry.Sharers.count()` to route Unblocks correctly:
   ```
   if (sharers > 0  ||  sender != GetS requestor) → Unblock_Stale
   else                                            → Unblock
   ```
   So the SS-derived flow's Unblocks are always stale (peer DLT_DATA is
   the legit completion, not Unblock); the M-derived flow's matching
   Unblock is the legit completion; everything else is stale.

2. **The `MT` state can never enter a delta transient.** Pairing on the
   M-side of an MT writeback is now blocked at `recordDelta` (v15), so
   no future demand request on M (which would otherwise hit the
   regular `d_/dd_` shipping handlers and ship delta bytes) can ever
   land on a paired entry.

`SS_DLT_GM_X` and `M_DLT_GM` were *not* merged because they share zero
events (the X-state collects INV-acks across `Ack/Ack_all/Dlt_GM_X_*`,
the M-state only handles Exclusive_Unblock). Merging would create a
state with a Frankenstein union of events -- harder to read, no
correctness benefit. The XOR Cache paper's "4 transient states" target
is real but requires the larger MSI+S0 / decoupled-tag-data rewrite,
documented in `IMPLEMENTATION_PLAN_v17.md` as future work.

---

## 5. Compression and decompression algorithm

### 5.1 Pair formation (compress)

When a line lands in L3 in a stable SS-side state (typically via
`(IS, Mem_Data, SS)` or `(MT_IIB, WB_Data, MT_SB)`), `recordDelta`
runs after the raw bytes have already been shipped to the L1
requestor. Sequence:

```
m_writeDataToCache               // 1. raw bytes from memory → cache_entry.DataBlk
e_sendDataToGetSRequestors       // 2. raw shipped to L1 (ensures L1 has raw)
recordDelta                      // 3. find partner + compress in place
```

`recordDelta` algorithm:

```
1. sig := generateMapValue(cache_entry.DataBlk)
   (10-bit hash sampling 8 bytes across the 64-byte line)
2. partner := mapTable.recordMapping(addr, dataBlk)
   - if hash slot empty:    store our addr, return 0
   - if hash slot occupied: return the stored addr (the partner)
3. if partner == 0: nothing to pair, return.
4. partner_entry := getCacheEntry(partner)
5. Check the unsafe-pair predicate -- abort if any of:
     (a) cache_entry.CacheState is IM   (heading to MT_MB)
     (b) cache_entry.CacheState is MT   (heading to M, zero sharers)  [v15]
     (c) partner.CacheState != SS       (partner unstable / unanchored)
     (d) partner.isDeltad                (partner already paired)
6. If safe:
     win_B_minus_A := computeAndStoreDelta(partner_addr, cache_entry_addr,
                                            partner_blk, cache_entry_blk)
     // computes both (B-A) and (A-B) byte-wise mod 256, picks whichever
     // has lower byte-sum, OVERWRITES both blocks with the chosen delta
   cache_entry.isDeltad := true; cache_entry.deltaPair := partner
   partner_entry.isDeltad := true; partner_entry.deltaPair := address
   if win_B_minus_A:
     cache_entry.deltaDirection   := true   (B = A + delta)
     partner_entry.deltaDirection := false  (A = B - delta)
   else:
     cache_entry.deltaDirection   := false
     partner_entry.deltaDirection := true
   incPairCount(mapTable)        // +1 active pair
```

After `recordDelta`, both L3 entries hold the same delta bytes; the
*direction* bit on each side is the inverse of the other side's, and
that's how each side knows the math to apply when reconstructing.

### 5.2 Pair dissolve (decompress)

There are 8 places where a pair gets dissolved (i.e., both sides return
to having raw bytes in L3 or one side is going away). The unifying
operation is `undeltaPair`:

```
undeltaPair(known_addr, partner_addr, known_blk, partner_blk, partner_dir):
   for i in 0..63:
     k := known_blk.getByte(i)
     d := partner_blk.getByte(i)         // delta byte (same on both sides)
     recovered := partner_dir ? (k + d) mod 256
                              : (k - d) mod 256
     partner_blk.setByte(i, recovered)
```

The caller must arrange for `known_blk` to actually hold raw bytes for
`known_addr` before invoking this. The four canonical sources of
"known raw bytes" the protocol can use:

1. **The L1 sharer's writeback** -- `(SS, L1_PUTM_Last, M)` arrives with
   raw bytes in the request message. `uu_undeltaIfPartnerDormant`
   adopts those into `cache_entry.DataBlk`, then undeltas the partner.
2. **The GETM requester's own copy** -- `(SS, L1_GETM_Dlt_SS, ...)` --
   the requestor was a sharer, so its GETM message carries raw B.
   `rdss_undeltaInlineFromGETM` adopts those bytes and undeltas the
   partner.
3. **A cross-fetch from a peer L1 sharer** -- when the requestor is a
   non-sharer or when the line is in M, L3 issues `FWD_PARTNER_DLT` to
   a sharer of the *other* side; that sharer's DLT_DATA response lands
   at the *other* address; `crm_`, `adgmx_`, `cev_`, or `sgsx_`
   handles the reconstruction depending on flow.
4. **A system-functional-read** (v17, only for functional access path) --
   `RubySystem::functionalRead` walks all controllers + memory and
   pulls partner's raw bytes from wherever they happen to live. This is
   the daxpy fix.

Every dissolve site updates `cache_entry.isDeltad := false` (and
similarly for partner if applicable), then calls `decPairCount(mapTable)`.

### 5.3 The functional-access reconstruction path (v17)

This is the path the daxpy SE-mode bug exercises. When
`functionalWrite(addr, pkt)` lands on a paired L3 entry:

```
functionalWrite:
  cache_entry := getCacheEntry(addr)
  if (is_valid(cache_entry) && cache_entry.isDeltad):
    partner := getCacheEntry(cache_entry.deltaPair)
    if (is_valid(partner)):
      // CRITICAL v17 step: recover partner's raw bytes BEFORE we
      // clear isDeltad. Otherwise partner.DataBlk stays as orphaned
      // delta bytes labeled as raw, and a future demand read on
      // partner ships those delta bytes to L1 → daxpy panic.
      recoverPartnerRawVoid(mapTable, cache_entry.deltaPair, partner.DataBlk)
      partner.isDeltad := false
    cache_entry.isDeltad := false
    decPairCount(mapTable)
  testAndWrite(addr, cache_entry.DataBlk, pkt)
```

`recoverPartnerRawVoid` calls `m_ruby_system->functionalRead(pkt)` on a
read packet for the partner's address. RubySystem iterates every
controller's `functionalRead` (each L0/L1/L2 implements its own) plus
the backing memory. The first one that has the partner address fills
the packet's buffer; we copy those bytes byte-by-byte into
`partner.DataBlk`. After this, partner's L3 storage genuinely holds
raw bytes -- future demand reads on partner ship the right thing.

The minimum-sharer invariant (Invariant A) guarantees that *some*
other place in the system has partner's raw at any moment a pair is
live, so the recovery succeeds in the common case.

---

## 6. The compression-ratio metric

`DeltaMapTable` registers a `statistics::Group` that prints under
`system.ruby.l2_cntrl0.deltaMapTable.*` in `stats.txt`:

```
pairsCreated                  Scalar   Lifetime pair formations
pairsDissolved                Scalar   Lifetime pair dissolutions
mapStoreEvents                Scalar   Map-table inserts that found no partner
mapOverrideEvents             Scalar   Map-table replaces (partner gone)
mapClearEvents                Scalar   Map-table clears (S→M unpaired)
activePairsAtEnd              Scalar   Live pairs at stats dump
peakActivePairs               Scalar   High-water mark
pairHitRate                   Formula  pairsCreated / (pairsCreated + mapStoreEvents)
pairSurvivalRate              Formula  (pairsCreated - pairsDissolved) / pairsCreated
compressionRatioInsertion     Formula  (2*pairsCreated + mapStoreEvents)
                                      / (pairsCreated + mapStoreEvents)
```

Read `compressionRatioInsertion` between 1.0 (nothing pairs, no gain)
and 2.0 (every line pairs, perfect 2× capacity gain). This is the
**logical** insertion-time ratio -- the SLICC controller in this branch
still uses `CacheMemory::lookup`, so paired lines occupy 2 physical
data slots. Realizing the physical capacity gain requires wiring the
controller through `DeltaCacheMemory`'s `m_delta_tag_array` /
`m_delta_data_array` (deferred; see `IMPLEMENTATION_PLAN_v17.md`).

`pairSurvivalRate` near 1.0 means pairs are durable in steady state --
good signal that the workload data is genuinely compressible. Near 0
means pairs form and immediately dissolve (churn), which is usually a
hash collision or pairing-against-volatile-data symptom.

---

## 7. Why the new design is correct (proof sketch)

For each demand request that reaches a paired L3 entry:

- **Direct GETS / GETM with a sharer carrying raw** (Conditions 1, SS
  paths): the requestor brings raw bytes itself. `rdss_` adopts them,
  undeltas partner inline. Both invariants restored.
- **Cross GETS / GETM (no requestor-carried raw)**: cross-fetch via
  `FWD_PARTNER_DLT` to a sharer of the *other* side; the response
  reaches the *other* address; `sgsx_` / `adgmx_` / `crm_` reconstruct
  there. The minimum-sharer invariant guarantees such a sharer exists
  (the unsafe-pair predicate refuses to create pairs that would
  violate it).
- **L3 eviction of a paired victim**: the `L2_Replacement_Delta`
  trigger routes through `cdt_captureDeltaToTBE` and either
  `SS_I_DLT` or `M_I_DLT`. Both end states do `udte_` or `cev_`
  reconstruction at the partner's address before the victim's TBE
  drains.
- **Functional access**: the v17 path recovers partner's raw via
  system-wide functional read, *before* the dissolve clears
  `isDeltad`. Demand reads after the dissolve land on the regular
  handlers, which now ship correct raw bytes.

Every path that could reach `partner.DataBlk` in a "raw bytes
expected" context goes through one of the four reconstruction sources
listed in §5.2 first. There's no remaining path where the regular
shipping handlers (`d_`, `dd_`, `ds_`, `e_`) can see delta bytes
labeled as raw.

---

## 8. Files edited

`src/mem/ruby/protocol/DeltaCache_Two_Level-L2cache.sm`
   - state declarations (added `DLT_GS`)
   - `recordDelta` predicate (added `cache_entry.CacheState == MT` block)
   - `recordDelta` success branch (added `incPairCount`)
   - eight `decPairCount` call sites at every pair-dissolve action
   - `functionalWrite` (added `recoverPartnerRawVoid` before dissolve)
   - external function declarations (`incPairCount`, `decPairCount`,
     `recoverPartnerRawVoid`)
   - Unblock dispatch in response in_port (sharer-count-driven gating)
   - DLT_DATA dispatch (collapsed `SS_DLT_GS || M_DLT_GS` checks to
     single `DLT_GS` check)
   - entry transitions for `L1_GETS_Dlt_Direct` and `L1_GETS_Dlt_Cross`
     redirected to `DLT_GS`
   - exit transitions on `DLT_GS` (`Dlt_GS_Data_Same → S_MB`,
     `Unblock → SS`, `Unblock_Stale` no-op)
   - stall lists collapsed `SS_DLT_GS, M_DLT_GS` → `DLT_GS`
   - broad `MEM_Inv` and `L2_Replacement*` stall sets updated

`src/mem/ruby/structures/deltacache/DeltaMapTable.hh`
   - include `<base/statistics.hh>` and forward-decl `class RubySystem`
   - new method declarations `incPairCount`, `decPairCount`,
     `recoverPartnerRaw`
   - new members `active_pairs`, `peak_active_pairs`, `m_ruby_system`
   - new `DeltaMapTableStats : public statistics::Group` struct with
     all Scalars and Formulas
   - inline SLICC global wrappers `incPairCount`, `decPairCount`,
     `recoverPartnerRaw`, `recoverPartnerRawVoid`

`src/mem/ruby/structures/deltacache/DeltaMapTable.cc`
   - includes for `mem/packet.hh`, `mem/request.hh`,
     `mem/ruby/system/RubySystem.hh`
   - constructor initializes `active_pairs(0)`, `peak_active_pairs(0)`,
     `m_ruby_system(p.ruby_system)`, `stats(this)`
   - `DeltaMapTableStats` constructor with `ADD_STAT` for every Scalar
     and the three Formula bindings (pairHitRate, pairSurvivalRate,
     compressionRatioInsertion)
   - `incPairCount` / `decPairCount` method bodies with stats updates
   - `recoverPartnerRaw` body (Request + Packet construction,
     `m_ruby_system->functionalRead(&pkt)`, byte-by-byte copy into
     target DataBlock)
   - counter increments wired into `recordMapping` (no-match branch),
     `overrideMapping`, `clearMapping`

`src/mem/ruby/structures/deltacache/DeltaMapTable.py`
   - new `ruby_system = Param.RubySystem(Parent.any, ...)` so the
     SimObject auto-finds the enclosing RubySystem at config time

