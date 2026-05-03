/*
 * DeltaCacheMemory implementation.
 * See DeltaCacheMemory.hh for design description.
 */

#include "mem/ruby/structures/DeltaCacheMemory.hh"

#include <cassert>

#include "base/logging.hh"

namespace gem5
{
namespace ruby
{

// ---------------------------------------------------------------------------
// DeltaTagEntry
// ---------------------------------------------------------------------------
void
DeltaTagEntry::print(std::ostream& out) const
{
    out << "[TagEntry"
        << " tagPtr=0x"   << std::hex << tagPtr
        << " deltaed="    << deltaed
        << " dir="        << direction
        << " deltaPtr=0x" << std::hex << deltaPtr
        << " dataPtr="    << std::dec << dataPtr
        << " valid="      << valid
        << "]";
}

// ---------------------------------------------------------------------------
// DeltaDataEntry
// ---------------------------------------------------------------------------
void
DeltaDataEntry::print(std::ostream& out) const
{
    out << "[DataEntry"
        << " tagPtr=0x" << std::hex << tagPtr
        << " valid="    << valid
        << "]";
}

// ---------------------------------------------------------------------------
// DeltaCacheMemory
// ---------------------------------------------------------------------------
DeltaCacheMemory::DeltaCacheMemory(const Params& p)
    : CacheMemory(p),
      m_num_delta_data_entries_param(p.num_data_entries)
{}

void
DeltaCacheMemory::init()
{
    // Run the standard CacheMemory initialisation first.
    // This sets m_cache_num_sets, m_cache_assoc, m_block_size, etc.
    CacheMemory::init();

    // Derive tag array size from CacheMemory's computed capacity.
    m_num_delta_tag_entries = getNumBlocks();  // = sets × assoc

    // Block size: CacheMemory keeps m_block_size private; recover it from
    // cache_size / num_blocks (both are valid after CacheMemory::init()).
    const int blk = getCacheSize() / m_num_delta_tag_entries;

    // Data array size: explicit param or auto (2× tag entries).
    if (m_num_delta_data_entries_param > 0) {
        m_num_delta_data_entries = m_num_delta_data_entries_param;
    } else {
        m_num_delta_data_entries = m_num_delta_tag_entries * 2;
    }

    fatal_if(m_num_delta_data_entries <= m_num_delta_tag_entries,
             "DeltaCacheMemory: num_data_entries (%d) must be greater than "
             "num_tag_entries (%d) to provide capacity gain.",
             m_num_delta_data_entries, m_num_delta_tag_entries);

    // Allocate arrays.
    m_delta_tag_array.resize(m_num_delta_tag_entries);
    m_delta_data_array.resize(m_num_delta_data_entries);

    for (auto& e : m_delta_data_array)
        e.initBlockSize(blk);

    // Build LIFO free-list stacks (slot 0 is popped first).
    m_free_delta_tag_slots.reserve(m_num_delta_tag_entries);
    for (int i = m_num_delta_tag_entries - 1; i >= 0; --i)
        m_free_delta_tag_slots.push_back(i);

    m_free_delta_data_slots.reserve(m_num_delta_data_entries);
    for (int i = m_num_delta_data_entries - 1; i >= 0; --i)
        m_free_delta_data_slots.push_back(i);
}

// ---------------------------------------------------------------------------
// Delta tag array
// ---------------------------------------------------------------------------
DeltaTagEntry&
DeltaCacheMemory::getDeltaTagEntry(int idx)
{
    assert(idx >= 0 && idx < m_num_delta_tag_entries);
    return m_delta_tag_array[idx];
}

const DeltaTagEntry&
DeltaCacheMemory::getDeltaTagEntry(int idx) const
{
    assert(idx >= 0 && idx < m_num_delta_tag_entries);
    return m_delta_tag_array[idx];
}

int
DeltaCacheMemory::findDeltaTagIndex(Addr tagAddr) const
{
    auto it = m_delta_tag_addr_to_idx.find(tagAddr);
    return (it != m_delta_tag_addr_to_idx.end()) ? it->second : -1;
}

void
DeltaCacheMemory::setDeltaTagEntry(int idx, Addr tagPtr, bool deltaed,
                                    bool direction, Addr deltaPtr,
                                    int dataPtr)
{
    assert(idx >= 0 && idx < m_num_delta_tag_entries);
    DeltaTagEntry& e = m_delta_tag_array[idx];

    if (e.isValid() && e.getTagPtr() != tagPtr)
        m_delta_tag_addr_to_idx.erase(e.getTagPtr());

    e.setTagPtr(tagPtr);
    e.setDeltaed(deltaed);
    e.setDirection(direction);
    e.setDeltaPtr(deltaPtr);
    e.setDataPtr(dataPtr);
    e.setValid(true);

    m_delta_tag_addr_to_idx[tagPtr] = idx;
}

int
DeltaCacheMemory::allocateDeltaTagSlot(Addr tagAddr)
{
    if (m_free_delta_tag_slots.empty())
        return -1;
    int idx = m_free_delta_tag_slots.back();
    m_free_delta_tag_slots.pop_back();
    m_delta_tag_array[idx].setTagPtr(tagAddr);
    m_delta_tag_array[idx].setValid(true);
    m_delta_tag_addr_to_idx[tagAddr] = idx;
    return idx;
}

void
DeltaCacheMemory::freeDeltaTagSlot(int idx)
{
    assert(idx >= 0 && idx < m_num_delta_tag_entries);
    DeltaTagEntry& e = m_delta_tag_array[idx];
    if (e.isValid())
        m_delta_tag_addr_to_idx.erase(e.getTagPtr());
    e.invalidate();
    m_free_delta_tag_slots.push_back(idx);
}

// ---------------------------------------------------------------------------
// Delta data array
// ---------------------------------------------------------------------------
DeltaDataEntry&
DeltaCacheMemory::getDeltaDataEntry(int idx)
{
    assert(idx >= 0 && idx < m_num_delta_data_entries);
    return m_delta_data_array[idx];
}

const DeltaDataEntry&
DeltaCacheMemory::getDeltaDataEntry(int idx) const
{
    assert(idx >= 0 && idx < m_num_delta_data_entries);
    return m_delta_data_array[idx];
}

void
DeltaCacheMemory::setDeltaDataEntry(int idx, Addr ownerTagAddr,
                                     const DataBlock& blk)
{
    assert(idx >= 0 && idx < m_num_delta_data_entries);
    DeltaDataEntry& e = m_delta_data_array[idx];
    e.setTagPtr(ownerTagAddr);
    e.setDataBlk(blk);
    e.setValid(true);
}

int
DeltaCacheMemory::allocateDeltaDataSlot(Addr ownerTagAddr)
{
    if (m_free_delta_data_slots.empty())
        return -1;
    int idx = m_free_delta_data_slots.back();
    m_free_delta_data_slots.pop_back();
    m_delta_data_array[idx].setTagPtr(ownerTagAddr);
    m_delta_data_array[idx].setValid(true);
    return idx;
}

void
DeltaCacheMemory::freeDeltaDataSlot(int idx)
{
    assert(idx >= 0 && idx < m_num_delta_data_entries);
    m_delta_data_array[idx].invalidate();
    m_free_delta_data_slots.push_back(idx);
}

// ---------------------------------------------------------------------------
// Dimension helpers
// ---------------------------------------------------------------------------
int
DeltaCacheMemory::getNumFreeDeltaTagSlots() const
{
    return static_cast<int>(m_free_delta_tag_slots.size());
}

int
DeltaCacheMemory::getNumFreeDeltaDataSlots() const
{
    return static_cast<int>(m_free_delta_data_slots.size());
}

// ---------------------------------------------------------------------------
// Print / stream
// ---------------------------------------------------------------------------
void
DeltaCacheMemory::print(std::ostream& out) const
{
    CacheMemory::print(out);
    out << " DeltaArrays["
        << " tagSlots="  << m_num_delta_tag_entries
        << " dataSlots=" << m_num_delta_data_entries
        << " freeTag="   << getNumFreeDeltaTagSlots()
        << " freeData="  << getNumFreeDeltaDataSlots()
        << " ]";
}

std::ostream&
operator<<(std::ostream& out, const DeltaCacheMemory& obj)
{
    obj.print(out);
    return out;
}

void
DeltaCacheMemory::preDumpStats()
{
    CacheMemory::preDumpStats();

    std::cout << "===== DeltaCacheMemory dump: " << name() << " =====\n";

    std::cout << "-- Delta tag array (" << m_num_delta_tag_entries
              << " slots) --\n";
    for (int i = 0; i < m_num_delta_tag_entries; ++i) {
        const DeltaTagEntry& e = m_delta_tag_array[i];
        if (!e.isValid())
            continue;
        std::cout << "  tagIdx=" << std::dec << i << " ";
        e.print(std::cout);
        std::cout << "\n";
    }

    std::cout << "-- Delta data array (" << m_num_delta_data_entries
              << " slots) --\n";
    for (int i = 0; i < m_num_delta_data_entries; ++i) {
        const DeltaDataEntry& e = m_delta_data_array[i];
        if (!e.isValid())
            continue;
        std::cout << "  dataIdx=" << std::dec << i << " ";
        e.print(std::cout);
        std::cout << "\n";
    }

    std::cout << "===== end dump: " << name() << " =====" << std::endl;
}

} // namespace ruby
} // namespace gem5
