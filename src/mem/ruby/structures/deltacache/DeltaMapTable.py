from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject


class DeltaMapTable(SimObject):
    type = "DeltaMapTable"
    cxx_header = "mem/ruby/structures/deltacache/DeltaMapTable.hh"
    cxx_class = "gem5::ruby::DeltaMapTable"

    block_size = Param.Int(64, "Default line size")
    latency = Param.Cycles(0, "Map table lookup latency")
    table_entries = Param.Int(1024, "Number of entries in the Map Table")
