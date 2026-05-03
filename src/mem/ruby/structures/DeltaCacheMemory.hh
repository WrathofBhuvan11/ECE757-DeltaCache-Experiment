/*
 * DeltaCacheMemory: Custom LLC that extends CacheMemory with two decoupled
 * arrays to support delta-compression between paired cache lines.
 *
 * Inherits the full CacheMemory interface (used unchanged by the SLICC
 * controller for coherence).  The two companion arrays — m_delta_tag_array
 * and m_delta_data_array — are initialised in init() and are available for
 * future compression logic.  For now they shadow the standard cache capacity
 * without modifying coherence behaviour.
 *
 * Tag array entry:  < tagPtr | deltaed | direction | deltaPtr | dataPtr >
 * Data array entry: < tagPtr | dataBlk >
 *
 * Field semantics:
 *   tagPtr    — tag (line address) of this cache line
 *   deltaed   — false=uncompressed/exclusive; true=delta-compressed with partner
 *   direction — 1-bit delta direction: false=A-B, true=B-A
 *   deltaPtr  — partner tag address (DELTA_NULL_ADDR when deltaed=false)
 *   dataPtr   — index into data array (DELTA_NULL_DATAPTR when unset)
 *   (data array) tagPtr — tag of the owning cache line (owner identification)
 *
 * Data array has more slots than tag array — extra slots = capacity gain.
 */

#ifndef __MEM_RUBY_STRUCTURES_DELTACACHEMEMORY_HH__
#define __MEM_RUBY_STRUCTURES_DELTACACHEMEMORY_HH__

#include <iostream>
#include <unordered_map>
#include <vector>

#include "mem/ruby/common/DataBlock.hh"
#include "mem/ruby/structures/CacheMemory.hh"
#include "params/DeltaRubyCache.hh"

namespace gem5
{
namespace ruby
{

static constexpr Addr DELTA_NULL_ADDR    = static_cast<Addr>(-1);
static constexpr int  DELTA_NULL_DATAPTR = -1;

// ---------------------------------------------------------------------------
// Tag array entry
// ---------------------------------------------------------------------------
class DeltaTagEntry
{
  public:
    DeltaTagEntry()
        : tagPtr(DELTA_NULL_ADDR),
          deltaed(false),
          direction(false),
          deltaPtr(DELTA_NULL_ADDR),
          dataPtr(DELTA_NULL_DATAPTR),
          valid(false)
    {}

    // --- setters ---
    void setTagPtr(Addr a)       { tagPtr    = a;   }
    void setDeltaed(bool d)      { deltaed   = d;   }
    void setDirection(bool dir)  { direction = dir; }
    void setDeltaPtr(Addr a)     { deltaPtr  = a;   }
    void setDataPtr(int dp)      { dataPtr   = dp;  }
    void setValid(bool v)        { valid     = v;   }

    // --- getters ---
    Addr getTagPtr()    const { return tagPtr;    }
    bool getDeltaed()   const { return deltaed;   }
    bool getDirection() const { return direction; }
    Addr getDeltaPtr()  const { return deltaPtr;  }
    int  getDataPtr()   const { return dataPtr;   }
    bool isValid()      const { return valid;     }

    void invalidate()
    {
        tagPtr    = DELTA_NULL_ADDR;
        deltaed   = false;
        direction = false;
        deltaPtr  = DELTA_NULL_ADDR;
        dataPtr   = DELTA_NULL_DATAPTR;
        valid     = false;
    }

    void print(std::ostream& out) const;

  public:
    // All fields public for direct access in addition to setters/getters.
    Addr tagPtr;    // tag (line address) of this cache line
    bool deltaed;   // false=uncompressed, true=delta-compressed with a partner
    bool direction; // 1-bit delta direction (false=A-B, true=B-A)
    Addr deltaPtr;  // partner tag address (DELTA_NULL_ADDR when deltaed=false)
    int  dataPtr;   // index into data array (DELTA_NULL_DATAPTR if unset)
    bool valid;     // is this slot occupied?
};

inline std::ostream&
operator<<(std::ostream& out, const DeltaTagEntry& e)
{
    e.print(out);
    return out;
}

// ---------------------------------------------------------------------------
// Data array entry
// ---------------------------------------------------------------------------
class DeltaDataEntry
{
  public:
    DeltaDataEntry()
        : tagPtr(DELTA_NULL_ADDR),
          valid(false)
    {}

    void initBlockSize(int block_size) { dataBlk.setBlockSize(block_size); }

    // --- setters ---
    void setTagPtr(Addr a)              { tagPtr  = a;   }
    void setDataBlk(const DataBlock& b) { dataBlk = b;   }
    void setByte(int idx, uint8_t val)  { dataBlk.setByte(idx, val); }
    void setValid(bool v)               { valid   = v;   }

    // --- getters ---
    Addr             getTagPtr()  const { return tagPtr;  }
    DataBlock&       getDataBlk()       { return dataBlk; }
    const DataBlock& getDataBlk() const { return dataBlk; }
    uint8_t          getByte(int idx) const { return dataBlk.getByte(idx); }
    bool             isValid()    const { return valid;   }

    void invalidate()
    {
        tagPtr = DELTA_NULL_ADDR;
        valid  = false;
    }

    void print(std::ostream& out) const;

  public:
    // All fields public for direct access in addition to setters/getters.
    Addr      tagPtr;   // tag of owning cache line (DELTA_NULL_ADDR = free)
    DataBlock dataBlk;  // cache line data (block_size bytes)
    bool      valid;    // is this slot occupied?
};

inline std::ostream&
operator<<(std::ostream& out, const DeltaDataEntry& e)
{
    e.print(out);
    return out;
}

// ---------------------------------------------------------------------------
// DeltaCacheMemory: extends CacheMemory — drop-in replacement for LLC
// ---------------------------------------------------------------------------
class DeltaCacheMemory : public CacheMemory
{
  public:
    typedef DeltaRubyCacheParams Params;

    DeltaCacheMemory(const Params& p);
    ~DeltaCacheMemory() = default;

    // Calls CacheMemory::init() then sizes the two decoupled delta arrays.
    void init() override;

    // -----------------------------------------------------------------------
    // Delta tag array — public element access
    // -----------------------------------------------------------------------

    DeltaTagEntry&       getDeltaTagEntry(int idx);
    const DeltaTagEntry& getDeltaTagEntry(int idx) const;

    // Address-based O(1) lookup; returns -1 if absent
    int findDeltaTagIndex(Addr tagAddr) const;

    // Write all 5 fields at once and update the lookup map
    void setDeltaTagEntry(int idx, Addr tagPtr, bool deltaed,
                          bool direction, Addr deltaPtr, int dataPtr);

    // Grab next free tag slot; returns index or -1 if full
    int allocateDeltaTagSlot(Addr tagAddr);

    // Return tag slot to free list
    void freeDeltaTagSlot(int idx);

    // -----------------------------------------------------------------------
    // Delta data array — public element access
    // -----------------------------------------------------------------------

    DeltaDataEntry&       getDeltaDataEntry(int idx);
    const DeltaDataEntry& getDeltaDataEntry(int idx) const;

    // Write both fields at once
    void setDeltaDataEntry(int idx, Addr ownerTagAddr, const DataBlock& blk);

    // Grab next free data slot; returns index or -1 if full
    int allocateDeltaDataSlot(Addr ownerTagAddr);

    // Return data slot to free list
    void freeDeltaDataSlot(int idx);

    // -----------------------------------------------------------------------
    // Delta array dimension / occupancy queries
    // -----------------------------------------------------------------------
    int getNumDeltaTagEntries()    const { return m_num_delta_tag_entries;  }
    int getNumDeltaDataEntries()   const { return m_num_delta_data_entries; }
    int getNumFreeDeltaTagSlots()  const;
    int getNumFreeDeltaDataSlots() const;

    void print(std::ostream& out) const;

    // Dump every valid tag-array and data-array entry to std::cout right
    // before gem5 writes the stats file, so the final L3 contents are
    // visible alongside the stats.
    void preDumpStats() override;

  private:
    // Configured via params; m_num_delta_tag_entries is derived from
    // CacheMemory::getNumBlocks() inside init().
    int m_num_delta_data_entries_param; // raw param value (0 = auto 2×)
    int m_num_delta_tag_entries  = 0;
    int m_num_delta_data_entries = 0;

    std::vector<DeltaTagEntry>  m_delta_tag_array;
    std::vector<DeltaDataEntry> m_delta_data_array;

    // Free-list stacks (LIFO)
    std::vector<int> m_free_delta_tag_slots;
    std::vector<int> m_free_delta_data_slots;

    // O(1) lookup: line address → delta tag array index
    std::unordered_map<Addr, int> m_delta_tag_addr_to_idx;
};

std::ostream& operator<<(std::ostream& out, const DeltaCacheMemory& obj);

} // namespace ruby
} // namespace gem5

#endif // __MEM_RUBY_STRUCTURES_DELTACACHEMEMORY_HH__
