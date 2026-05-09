#include "mem/cache/compressors/delta_cache.hh"

#include <memory>

namespace gem5
{
namespace compression
{

DeltaCacheCompressor::DeltaCacheCompressor(const Params &p)
    : Base(p), mapTable(mapTableSize, 0)
{
    std::cout << "!!! DELTA CACHE OBJECT CREATED !!!" << std::endl;
}

uint32_t
DeltaCacheCompressor::calculateSBLHash(const std::vector<Chunk>& chunks)
{
    uint32_t mapValue = 0;

    for (const auto& word : chunks) {
        // SBL: Look at the 6 most significant bytes of every 8-byte word
        for (int i = 2; i < 8; i++) {
            uint8_t byte = (word >> (i * 8)) & 0xFF;
            bool label = (byte != 0);

            // Simple XOR folding into a 10-bit space
            mapValue ^= (label << (i % 10));
        }
    }
    return mapValue & (mapTableSize - 1); // Ensure it fits the table size
}

std::unique_ptr<Base::CompressionData>
DeltaCacheCompressor::compress(const std::vector<Chunk>& chunks,
    Cycles& comp_lat, Cycles& decomp_lat)
{
    uint32_t index = calculateSBLHash(chunks);

    if(mapTable[index] != 0){
        inform("MATCH: Index [%d] | Pair Formed: (New: 0x%llx, Base: 0x%ll)\n", index, chunks[0], mapTable[index]);
        mapTable[index] = 0;
    }

    else{
        mapTable[index] = chunks[0];
    }
    // Set latencies based on the parameters defined in the SimObject
    comp_lat = Cycles(0);
    decomp_lat = Cycles(0);

    std::unique_ptr<CompData> comp_data(new CompData(chunks));

    // 4. Set size to FULL block size (e.g., 64 bytes * 8 bits = 512)
    // This ensures NO capacity compression happens.
    comp_data->setSizeBits(blkSize * 8);

    return std::move(comp_data);
}

void
DeltaCacheCompressor::decompress(const CompressionData* comp_data,
                                uint64_t* cache_line)
{
    // Cast the generic CompressionData back to our specific CompData
    const CompData* const_comp_data = static_cast<const CompData*>(comp_data);

    // Reconstruct original data from chunks
    fromChunks(const_comp_data->chunks, cache_line);
}

} // namespace compression
} // namespace gem5
