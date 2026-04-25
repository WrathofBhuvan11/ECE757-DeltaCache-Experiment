#ifndef __MEM_RUBY_STRUCTURES_DELTA_MAP_TABLE_HH__
#define __MEM_RUBY_STRUCTURES_DELTA_MAP_TABLE_HH__

#include "params/DeltaMapTable.hh"
#include "sim/sim_object.hh"
#include "mem/ruby/common/Address.hh"
#include "mem/ruby/common/DataBlock.hh"

namespace gem5 {
namespace ruby {

class DeltaMapTable : public SimObject {
  public:
    typedef DeltaMapTableParams Params;
    DeltaMapTable(const Params &p);

    int calculateCompressedSize(const DataBlock& blk);
    void recordMapping(Addr addr, int compressed_size);

  private:
    int m_block_size;
};

/**
 * Global Wrapper Functions
 * These are required because SLICC calls these as global functions 
 * from the L2 Controller.
 */
inline int calculateCompressedSize(DeltaMapTable& table, const DataBlock& blk) {
    return table.calculateCompressedSize(blk);
}

inline void recordMapping(DeltaMapTable& table, Addr addr, int size) {
    table.recordMapping(addr, size);
}

} // namespace ruby
} // namespace gem5
#endif
