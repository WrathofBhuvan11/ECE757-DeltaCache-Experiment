from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject

class DeltaMapTable(SimObject):
    type = 'DeltaMapTable'
    cxx_header = "mem/ruby/structures/deltacache/DeltaMapTable.hh"
    cxx_class = 'gem5::ruby::DeltaMapTable'

    block_size = Param.Int(64, "Default line size")
    latency = Param.Cycles(1, "Map table lookup latency")
