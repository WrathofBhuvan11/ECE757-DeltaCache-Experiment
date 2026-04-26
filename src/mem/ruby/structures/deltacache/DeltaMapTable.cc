#include "mem/ruby/structures/deltacache/DeltaMapTable.hh"
#include <iostream>

namespace gem5 {
namespace ruby {

DeltaMapTable::DeltaMapTable(const Params &p)
    : SimObject(p), m_block_size(p.block_size), m_table_entries(p.table_entries)
{
    m_direct_map_table.resize(m_table_entries,0);
    m_valid_bits.resize(m_table_entries,false);
    // Note: Use gem5's warn/inform instead of std::cout for better logging
    inform("DeltaMapTable: Skeleton Initialized for ECE757 DeltaCache\n");
}

uint64_t 
DeltaMapTable::generateMapValue(const DataBlock& blk)
{
    uint64_t byte_labels = 0;
    for (int i = 0; i < 64; ++i) {
        if (blk.getByte(i) != 0) {
            byte_labels |= (1ULL << i);
        }
    }
    // Folding to mix the entropy
    byte_labels ^= (byte_labels >> 32);
    byte_labels ^= (byte_labels >> 16);
    return byte_labels;
}


int DeltaMapTable::calculateCompressedSize(const DataBlock& blk) {
    // Placeholder: Return half the block size (usually 32 for a 64B block)
    return m_block_size / 2;
}

void DeltaMapTable::recordMapping(Addr addr, const DataBlock& blk) {
    //inform("DEBUG: recordDelta logic reached\n");
    // inform("DeltaMapTable: Recording 0x%lx with size %d\n", addr, compressed_size);
    uint64_t map_val = generateMapValue(blk);
    
    // Direct index using modulo to stay within bounds
    uint32_t index = map_val % m_table_entries;

    if (m_valid_bits[index]) {
        Addr candidate_addr = m_direct_map_table[index];
        
        if (candidate_addr != addr) {
            inform("ECE757 Delta Match: Index %d | Line 0x%lx matched Candidate 0x%lx\n", 
                   index, addr, candidate_addr);
            m_valid_bits[index] = false;
        }
    } else {
        m_direct_map_table[index] = addr;
        m_valid_bits[index] = true;
        inform("ECE757 Delta Store: Index %d | Line 0x%lx stored\n", index, addr);
    }
}

} // namespace ruby
} // namespace gem5
