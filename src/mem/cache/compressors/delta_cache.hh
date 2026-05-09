#ifndef __MEM_CACHE_COMPRESSORS_DELTA_CACHE_HH__
#define __MEM_CACHE_COMPRESSORS_DELTA_CACHE_HH__

#include <memory>
#include <vector>

#include "mem/cache/compressors/base.hh"
#include "params/DeltaCacheCompressor.hh"

namespace gem5
{
namespace compression
{

class DeltaCacheCompressor : public Base
{
  protected:
    /**
     * Internal class to store compressed data information.
     */
    class CompData;

    std::unique_ptr<CompressionData> compress(
        const std::vector<Chunk>& chunks,
        Cycles& comp_lat, Cycles& decomp_lat) override;

    std::vector<uint64_t> mapTable;
    const size_t mapTableSize = 1024; // 10-bit index

    uint32_t calculateSBLHash(const std::vector<Chunk>& chunks);

    void decompress(const CompressionData* comp_data,
                    uint64_t* cache_line) override;

  public:
    typedef DeltaCacheCompressorParams Params;
    DeltaCacheCompressor(const Params &p);
    ~DeltaCacheCompressor() = default;
};

/**
 * DeltaCache-specific compression data.
 */
class DeltaCacheCompressor::CompData : public CompressionData
{
  public:
    std::vector<Chunk> chunks;

    CompData(const std::vector<Chunk>& chunks)
      : CompressionData(), chunks(chunks)
    {
    }
    ~CompData() = default;
};

} // namespace compression
} // namespace gem5

#endif // __MEM_CACHE_COMPRESSORS_DELTA_CACHE_HH__
