#ifndef __MEM_RUBY_STRUCTURES_DELTA_MAP_TABLE_HH__
#define __MEM_RUBY_STRUCTURES_DELTA_MAP_TABLE_HH__

#include <iomanip>
#include <sstream>

#include "base/logging.hh"
#include "mem/ruby/common/Address.hh"
#include "mem/ruby/common/DataBlock.hh"
#include "params/DeltaMapTable.hh"
#include "sim/sim_object.hh"

namespace gem5 {
namespace ruby {

class DeltaMapTable : public SimObject {
  public:
    typedef DeltaMapTableParams Params;
    DeltaMapTable(const Params &p);

    int calculateCompressedSize(const DataBlock& blk);
    // Returns the partner line address on a hash match (and clears the slot),
    // or 0 if this address was just stored (no match yet).
    Addr recordMapping(Addr addr, const DataBlock& blk);
    // Unconditionally store addr at this block's hash slot. Used when
    // recordMapping reported a match but the partner was no longer cached,
    // so this addr becomes the new candidate for future pairings.
    void overrideMapping(Addr addr, const DataBlock& blk);
    // Clear addr's slot (computed from blk's signature) iff it currently
    // holds addr. Used when an unpaired line transitions to L3-only / M:
    // it's no longer a worthwhile pairing candidate (no upper sharer ever
    // re-anchored its content), so drop it from the map table.
    void clearMapping(Addr addr, const DataBlock& blk);

  private:
    int pair_count; // For tracking the number of matched pairs
    int m_block_size;
    int m_table_entries; // table size
    std::vector<Addr> m_direct_map_table; //the map table of m_table_entries size
    std::vector<bool> m_valid_bits; //entry valid or not
    uint64_t generateMapValue(const DataBlock& blk);
};

/**
 * Global Wrapper Functions
 * These are required because SLICC calls these as global functions
 * from the L2 Controller.
 */
inline int calculateCompressedSize(DeltaMapTable& table, const DataBlock& blk) {
    return table.calculateCompressedSize(blk);
}

inline Addr recordMapping(DeltaMapTable& table, Addr addr, const DataBlock& blk) {
    return table.recordMapping(addr, blk);
}

// Void variant for SLICC call sites that don't want to capture the partner
// address (e.g., re-adding the surviving partner to the map table after a
// Condition 2 undelta).
inline void
recordMappingVoid(DeltaMapTable &table, Addr addr, const DataBlock &blk)
{
    (void)table.recordMapping(addr, blk);
}

inline void overrideMapping(DeltaMapTable& table, Addr addr, const DataBlock& blk) {
    table.overrideMapping(addr, blk);
}

inline void clearMapping(DeltaMapTable& table, Addr addr, const DataBlock& blk) {
    table.clearMapping(addr, blk);
}

/**
 * Compute byte-wise (B - A) and (A - B) mod 256, pick the variant with the
 * smaller byte-sum (lower entropy), and overwrite both DataBlocks with the
 * winning delta. Returns true when (B - A) won (i.e. B = A + delta), false
 * when (A - B) won (i.e. B = A - delta). Caller uses the returned direction
 * to set the per-entry deltaDirection metadata.
 */
inline bool computeAndStoreDelta(Addr a_addr, Addr b_addr,
                                 DataBlock& a_blk, DataBlock& b_blk) {

    auto blkToHex = [](const DataBlock& blk) {
        std::ostringstream oss;
        for (int i = 0; i < 64; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0')
                << (int)blk.getByte(i);
            if (i != 63) oss << ' ';
        }
        return oss.str();
    };

    inform("Before A data (0x%lx): %s", a_addr, blkToHex(a_blk).c_str());
    inform("Before B data (0x%lx): %s", b_addr, blkToHex(b_blk).c_str());

    uint8_t deltas_BA[64];
    uint8_t deltas_AB[64];
    uint32_t score_BA = 0;
    uint32_t score_AB = 0;
    for (int i = 0; i < 64; ++i) {
        deltas_BA[i] = (uint8_t)(b_blk.getByte(i) - a_blk.getByte(i));
        deltas_AB[i] = (uint8_t)(a_blk.getByte(i) - b_blk.getByte(i));
        score_BA += deltas_BA[i];
        score_AB += deltas_AB[i];
    }
    bool win_B_minus_A = (score_BA <= score_AB);
    const uint8_t* chosen = win_B_minus_A ? deltas_BA : deltas_AB;
    for (int i = 0; i < 64; ++i) {
        a_blk.setByte(i, chosen[i]);
        b_blk.setByte(i, chosen[i]);
    }



    std::ostringstream delta_hex;
    for (int i = 0; i < 64; ++i) {
        delta_hex << std::hex << std::setw(2) << std::setfill('0')
                  << (int)chosen[i];
        if (i != 63) delta_hex << ' ';
    }

    inform("After A data (0x%lx): %s", a_addr, blkToHex(a_blk).c_str());
    inform("After B data (0x%lx): %s", b_addr, blkToHex(b_blk).c_str());
    inform("ECE757 Delta Pair: A=0x%lx B=0x%lx | dir=%s | "
           "score_BA=%u score_AB=%u | delta=[%s]\n",
           a_addr, b_addr,
           win_B_minus_A ? "B-A (B=A+delta, A=B-delta)"
                         : "A-B (B=A-delta, A=B+delta)",
           score_BA, score_AB, delta_hex.str().c_str());
    return win_B_minus_A;
}

/**
 * Reconstruct the partner's original bytes from a known-original line and
 * the delta currently stored in the partner's DataBlk.
 *
 * Convention from computeAndStoreDelta:
 *   direction == true  => "this entry's original = partner's original + delta"
 *   direction == false => "this entry's original = partner's original - delta"
 *
 * known_blk holds the known-original bytes (e.g., a fresh PUTM writeback).
 * partner_blk holds the delta bytes; on return it is overwritten in place
 * with the reconstructed originals.
 */
inline void
undeltaPair(Addr known_addr, Addr partner_addr, const DataBlock &known_blk,
            DataBlock &partner_blk, bool partner_dir)
{
    auto blkToHex = [](const DataBlock &blk) {
        std::ostringstream oss;
        for (int i = 0; i < 64; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0')
                << (int)blk.getByte(i);
            if (i != 63) {
                oss << ' ';
            }
        }
        return oss.str();
    };

    inform(
        "ECE757 Undelta begin: known=0x%lx data=%s | partner=0x%lx delta=%s",
        known_addr, blkToHex(known_blk).c_str(), partner_addr,
        blkToHex(partner_blk).c_str());

    for (int i = 0; i < 64; ++i) {
        uint8_t k = known_blk.getByte(i);
        uint8_t d = partner_blk.getByte(i);
        uint8_t recovered = partner_dir ? (uint8_t)(k + d) : (uint8_t)(k - d);
        partner_blk.setByte(i, recovered);
    }

    inform("ECE757 Undelta done: partner=0x%lx data=%s | dir=%s", partner_addr,
           blkToHex(partner_blk).c_str(),
           partner_dir ? "ADD (partner=known+delta)"
                       : "SUB (partner=known-delta)");
}

} // namespace ruby
} // namespace gem5
#endif
