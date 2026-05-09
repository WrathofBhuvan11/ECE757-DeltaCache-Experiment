import m5

from gem5.components.boards.test_board import TestBoard
from gem5.components.cachehierarchies.ruby.deltacache_cache_hierarchy import (
    DeltaCacheCacheHierarchy,
)
from gem5.components.memory.single_channel import SingleChannelDDR4_2400
from gem5.components.processors.random_generator import RandomGenerator
from gem5.simulate.simulator import Simulator

# 1. Setup the Ruby Cache Hierarchy
# You can customize sizes, associativities, and the number of banks here.
cache_hierarchy = DeltaCacheCacheHierarchy(
    l1i_size="32KiB",
    l1i_assoc=8,
    l1d_size="32KiB",
    l1d_assoc=8,
    l2_size="256KiB",
    l2_assoc=16,
    l3_size="512KiB",
    l3_assoc=64,
    num_l3_banks=2,
)

# 2. Setup the Main Memory
# Single channel DDR4 is standard, but you can swap this for HBM, LPDDR, etc.
memory = SingleChannelDDR4_2400(size="1GiB")

# 3. Setup the "CPU" (Traffic Generator)
# This replaces the old RubyRandomTester. It generates random memory requests.
generator = RandomGenerator(
    num_cores=2,  # Number of generator cores (acting as 2 CPUs)
    duration="1ms",  # How long to run the traffic
    rate="10GB/s",  # The injection rate of the requests
    block_size=64,  # Cache line size
    min_addr=0,  # Memory address range start
    max_addr=100000,  # Memory address range end
    rd_perc=50,  # 50% reads, 50% writes
    data_limit=0,  # 0 means no limit, rely on 'duration'
)

# 4. Assemble the System on a TestBoard
# The TestBoard automatically handles hooking up the clock, voltage, and ports.
board = TestBoard(
    clk_freq="1GHz",  # Setting this to 1GHz prevents those rounding warnings!
    generator=generator,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# 5. Run the Simulation
simulator = Simulator(board=board)

print("Starting custom Ruby simulation...")
simulator.run()
print("Simulation finished successfully!")
