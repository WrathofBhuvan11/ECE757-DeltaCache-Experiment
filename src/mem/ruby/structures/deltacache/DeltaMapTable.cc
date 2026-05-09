#include "mem/ruby/structures/deltacache/DeltaMapTable.hh"

#include <iostream>
#include <memory>

#include "mem/packet.hh"
#include "mem/request.hh"
#include "mem/ruby/system/RubySystem.hh"

namespace gem5 {
namespace ruby {

DeltaMapTable::DeltaMapTable(const Params &p)
    : SimObject(p), pair_count(0), active_pairs(0), peak_active_pairs(0),
      m_block_size(p.block_size), m_table_entries(p.table_entries),
      m_ruby_system(p.ruby_system),
      stats(this)
{
    m_direct_map_table.resize(m_table_entries,0);
    m_valid_bits.resize(m_table_entries,false);
    // Note: Use gem5's warn/inform instead of std::cout for better logging
    inform("DeltaMapTable: Skeleton Initialized for ECE757 DeltaCache\n");
}

// ECE757 v17: walk the system to recover partner's raw bytes.
// RubySystem::functionalRead(pkt) iterates every controller's
// functionalRead() and the backing memory; the first controller (or
// memory) that has the address copies its current bytes into the
// packet. We pre-bind the packet's data buffer to target_blk's storage
// so the result lands directly where SLICC will use it.
bool
DeltaMapTable::recoverPartnerRaw(Addr partner_addr, DataBlock &target_blk)
{
    if (!m_ruby_system) {
        warn_once("DeltaMapTable::recoverPartnerRaw: m_ruby_system unset; "
                  "cannot recover partner 0x%lx -- v17 functionalWrite "
                  "fix is a no-op without it. Wire ruby_system param in "
                  "DeltaMapTable.py.", partner_addr);
        return false;
    }

    const int blk = m_block_size;
    RequestPtr req = std::make_shared<Request>(partner_addr, blk,
                                                /*flags*/ 0,
                                                /*requestor*/ Request::funcRequestorId);
    Packet pkt(req, MemCmd::ReadReq);
    // Use a heap buffer; we'll copy into target_blk after the read.
    auto buf = std::make_unique<uint8_t[]>(blk);
    pkt.dataStatic(buf.get());

    // gem5 RubySystem signature in this tree: bool functionalRead(Packet*).
    bool ok = m_ruby_system->functionalRead(&pkt);
    if (!ok) {
        warn_once("DeltaMapTable: recoverPartnerRaw 0x%lx -> miss "
                  "(no controller / memory had the line)", partner_addr);
        return false;
    }

    // Copy the bytes the system wrote into our buffer back into target_blk.
    for (int i = 0; i < blk; ++i) {
        target_blk.setByte(i, buf[i]);
    }
    inform("ECE757 v17 recoverPartnerRaw: 0x%lx recovered (%d bytes)\n",
           partner_addr, blk);
    return true;
}

// ECE757: stats group constructor. ADD_STAT wires each member to its
// printed name and description. Formulas are bound after declaration so
// they can reference the Scalar members declared in the same struct.
DeltaMapTable::DeltaMapTableStats::DeltaMapTableStats(
        statistics::Group *parent)
    : statistics::Group(parent, "deltaMapTable"),
      ADD_STAT(pairsCreated,        statistics::units::Count::get(),
               "Number of delta pairs successfully created (incPairCount)"),
      ADD_STAT(pairsDissolved,      statistics::units::Count::get(),
               "Number of delta pairs dissolved on undelta (decPairCount)"),
      ADD_STAT(mapStoreEvents,      statistics::units::Count::get(),
               "recordMapping calls that stored the line (no partner found)"),
      ADD_STAT(mapOverrideEvents,   statistics::units::Count::get(),
               "overrideMapping calls (partner evicted, this line replaces it)"),
      ADD_STAT(mapClearEvents,      statistics::units::Count::get(),
               "clearMapping calls that actually removed an entry"),
      ADD_STAT(activePairsAtEnd,    statistics::units::Count::get(),
               "Snapshot of active delta pairs when stats were dumped"),
      ADD_STAT(peakActivePairs,     statistics::units::Count::get(),
               "Peak (high-water mark) of simultaneously-live delta pairs"),
      ADD_STAT(pairSurvivalRate,    statistics::units::Ratio::get(),
               "Fraction of created pairs that survived to stats dump: "
               "(pairsCreated - pairsDissolved) / pairsCreated"),
      ADD_STAT(pairHitRate,         statistics::units::Ratio::get(),
               "pairsCreated / (pairsCreated + mapStoreEvents) — fraction "
               "of map-table inserts that landed on an existing partner"),
      ADD_STAT(compressionRatioInsertion, statistics::units::Ratio::get(),
               "Insertion-time LLC compression ratio: "
               "(2*pairsCreated + mapStoreEvents) / "
               "(pairsCreated + mapStoreEvents). 1.0 = nothing paired, "
               "2.0 = every line paired.")
{
    // pairHitRate = pairsCreated / (pairsCreated + mapStoreEvents)
    pairHitRate = pairsCreated /
                  (pairsCreated + mapStoreEvents);

    // pairSurvivalRate = (pairsCreated - pairsDissolved) / pairsCreated
    pairSurvivalRate = (pairsCreated - pairsDissolved) / pairsCreated;

    // compressionRatioInsertion = (2*pairsCreated + mapStoreEvents) /
    //                             (pairsCreated  + mapStoreEvents)
    compressionRatioInsertion =
        (pairsCreated + pairsCreated + mapStoreEvents) /
        (pairsCreated + mapStoreEvents);
}

// ECE757: pair lifecycle hooks called from SLICC actions in L2cache.sm.
void
DeltaMapTable::incPairCount()
{
    pair_count++;
    active_pairs++;
    if (active_pairs > peak_active_pairs) {
        peak_active_pairs = active_pairs;
        stats.peakActivePairs = peak_active_pairs;
    }
    stats.pairsCreated++;
    stats.activePairsAtEnd = active_pairs;
    inform("ECE757 incPairCount: active_pairs=%d peak=%d (lifetime=%d)\n",
           active_pairs, peak_active_pairs, pair_count);
}

void
DeltaMapTable::decPairCount()
{
    if (active_pairs > 0) {
        active_pairs--;
    }
    stats.pairsDissolved++;
    stats.activePairsAtEnd = active_pairs;
    inform("ECE757 decPairCount: active_pairs=%d\n", active_pairs);
}

//uint64_t
//DeltaMapTable::generateMapValue(const DataBlock& blk)
//{
//    uint64_t byte_labels = 0;
//    for (int i = 0; i < 64; ++i) {
//        if (blk.getByte(i) != 0) {
//            byte_labels |= (1ULL << i);
//        }
//    }
//    // Folding to mix the entropy
//    byte_labels ^= (byte_labels >> 32);
//    byte_labels ^= (byte_labels >> 16);
//    return byte_labels;
//}



/////////////////////////////////////////////////
///////////// SBL Implementation ////////////////
/////////////////////////////////////////////////

uint64_t
DeltaMapTable::generateMapValue(const DataBlock& blk)
{
    uint64_t signature = 0;

    // SBL Implementation: Sample 8 bytes across the 64B line
    // Indices 0, 8, 16, 24, 32, 40, 48, 56 cover the full line spread
    /*for (int i = 0; i < 8; ++i) {
        uint8_t byte = blk.getByte(i * 8);
        // Basic hashing: Shift and XOR to mix byte values into the signature
        signature ^= (static_cast<uint64_t>(byte) << (i % 4));
    }

    // Mix entropy further
    signature ^= (signature >> 8);*/
    //signature = (blk.getByte(0) << 0)  | (blk.getByte(8) << 8)  | (blk.getByte(16) << 16) | (blk.getByte(24) << 24) |
    //            (blk.getByte(32) << 32) | (blk.getByte(40) << 40) | (blk.getByte(48) << 48) | (blk.getByte(56) << 56);
    for (int i = 0; i < 8; ++i) {
        uint8_t byte = blk.getByte(i * 8);
        signature ^= (static_cast<uint64_t>(byte) << (i % 3));
    }
        signature ^= (signature >> 8);
    // Mask to exactly 10 bits (0 to 1023)
    return signature % 0x3FF;
}


int DeltaMapTable::calculateCompressedSize(const DataBlock& blk) {
    // Placeholder: Return half the block size (usually 32 for a 64B block)
    return m_block_size / 2;
}

Addr DeltaMapTable::recordMapping(Addr addr, const DataBlock& blk) {
    uint64_t sig_val = generateMapValue(blk);

    // Since sig_val is now 10 bits, ensure m_table_entries matches (1024)
    uint32_t index = sig_val % m_table_entries;

    if (m_valid_bits[index]) {
        Addr candidate_addr = m_direct_map_table[index];

        if (candidate_addr != addr) {
            pair_count++;
            inform("ECE757 Delta Match: Sig [0x%x] | Index %d | Line 0x%lx matched Candidate 0x%lx, Pair Count: %d\n",
                   sig_val, index, addr, candidate_addr, pair_count);

            // Slot is consumed by the pairing.
            m_valid_bits[index] = false;
            m_direct_map_table[index] = 0;
            return candidate_addr;
        }
        // Same address re-hashed — treat as no new pair.
        return 0;
    }

    m_direct_map_table[index] = addr;
    m_valid_bits[index] = true;
    stats.mapStoreEvents++;
    inform("ECE757 Delta Store: Sig [0x%x] | Index %d | Line 0x%lx stored\n",
           sig_val, index, addr);
    return 0;
}

void DeltaMapTable::overrideMapping(Addr addr, const DataBlock& blk) {
    uint64_t sig_val = generateMapValue(blk);
    uint32_t index = sig_val % m_table_entries;
    m_direct_map_table[index] = addr;
    m_valid_bits[index] = true;
    stats.mapOverrideEvents++;
    inform("ECE757 Delta Override: Sig [0x%x] | Index %d | Line 0x%lx stored (partner evicted)\n",
           sig_val, index, addr);
}

void DeltaMapTable::clearMapping(Addr addr, const DataBlock& blk) {
    uint64_t sig_val = generateMapValue(blk);
    uint32_t index = sig_val % m_table_entries;
    if (m_valid_bits[index] && m_direct_map_table[index] == addr) {
        m_valid_bits[index] = false;
        m_direct_map_table[index] = 0;
        stats.mapClearEvents++;
        inform("ECE757 Delta Clear: Sig [0x%x] | Index %d | Line 0x%lx removed (S->M unpaired)\n",
               sig_val, index, addr);
    }
}



} // namespace ruby
} // namespace gem5
