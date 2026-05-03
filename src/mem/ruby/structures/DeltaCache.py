from m5.objects.RubyCache import RubyCache
from m5.params import *


class DeltaRubyCache(RubyCache):
    type = "DeltaRubyCache"
    cxx_class = "gem5::ruby::DeltaCacheMemory"
    cxx_header = "mem/ruby/structures/DeltaCacheMemory.hh"

    # Number of data array slots.
    # Must be greater than the tag array size (= size / block_size).
    # 0 = auto: 2x the tag count, set inside DeltaCacheMemory::init().
    num_data_entries = Param.Int(
        0,
        "data array slots; 0 = auto 2x tag count (always > tag count)",
    )
