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
    # ECE757 v17: RubySystem ref for system-wide functional reads.
    # Needed by recoverPartnerRaw() so functionalWrite on a delta-paired
    # entry can recover the partner's raw bytes from any L1 sharer or
    # memory before the v12 dissolve-pair logic strands the partner with
    # delta bytes flagged as raw. Parent.any auto-resolves at config
    # time -- no Python config-script change needed by the user.
    ruby_system = Param.RubySystem(Parent.any, "RubySystem for functional access")
