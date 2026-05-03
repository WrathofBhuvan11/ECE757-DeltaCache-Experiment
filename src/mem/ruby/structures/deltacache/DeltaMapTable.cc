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
    for (int i = 0; i < 8; ++i) {
        uint8_t byte = blk.getByte(i * 8);
        // Basic hashing: Shift and XOR to mix byte values into the signature
        signature ^= (static_cast<uint64_t>(byte) << (i % 4)); 
    }

    // Mix entropy further
    signature ^= (signature >> 8);
    
    // Mask to exactly 10 bits (0 to 1023)
    return signature & 0x3FF; 
}


int DeltaMapTable::calculateCompressedSize(const DataBlock& blk) {
    // Placeholder: Return half the block size (usually 32 for a 64B block)
    return m_block_size / 2;
}

void DeltaMapTable::recordMapping(Addr addr, const DataBlock& blk) {
    uint64_t sig_val = generateMapValue(blk);
    
    // Since sig_val is now 10 bits, ensure m_table_entries matches (1024)
    uint32_t index = sig_val % m_table_entries;

    if (m_valid_bits[index]) {
        Addr candidate_addr = m_direct_map_table[index];
        
        if (candidate_addr != addr) {
            // Updated to print the 10-bit signature value
            inform("ECE757 Delta Match: Sig [0x%x] | Index %d | Line 0x%lx matched Candidate 0x%lx\n", 
                   sig_val, index, addr, candidate_addr);
            
            // In a real XOR cache, you'd trigger the Delta Compressor here
            m_valid_bits[index] = false; 
            m_direct_map_table[index] = 0;
        }
    } else {
        m_direct_map_table[index] = addr;
        m_valid_bits[index] = true;
        inform("ECE757 Delta Store: Sig [0x%x] | Index %d | Line 0x%lx stored\n", 
               sig_val, index, addr);
    }
}



} // namespace ruby
} // namespace gem5
