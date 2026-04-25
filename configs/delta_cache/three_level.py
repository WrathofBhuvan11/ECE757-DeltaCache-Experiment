# import the m5 (gem5) library created when gem5 is built
import m5
import os

# import all of the SimObjects
from m5.objects import *
from m5.objects import DeltaCacheCompressor

# Add the common scripts to our path
m5.util.addToPath("../")

# import the caches which we made (your L1Cache/L1ICache/L1DCache/L2Cache)
from caches import L1ICache, L1DCache, L2Cache
from cpu import MyMinorCPU

# import the SimpleOpts module
from common import SimpleOpts

# -------------------------
# Command-line options
# -------------------------
SimpleOpts.add_option("--clk", help="System clock frequency", default="1GHz")
SimpleOpts.add_option("--fpu_operation_latency", help="fpu_operation_latency", default="6")
SimpleOpts.add_option("--fpu_issue_latency", help="fpu_issue_latency", default="1")

# ⭐ ADD THESE TWO LINES ⭐
SimpleOpts.add_option("--cmd", help="Binary to run in SE mode", default="")
SimpleOpts.add_option("--options", help="Arguments for the binary", default="")

args = SimpleOpts.parse_args()

# ⭐ REMOVE THIS (hardcoded binary)
# binary = "configs/somani/hw2/dapxy"

# ⭐ USE THE USER-PROVIDED BINARY
binary = os.path.expanduser(args.cmd)

clk = args.clk

# -------------------------
# Define an L3 cache (reuse L2 wiring)
# -------------------------
class L3Cache(Cache): # Inherit from Cache directly
    size = '2MiB'
    assoc = 16
    tag_latency = 20
    data_latency = 20
    response_latency = 20
    mshrs = 20
    tgts_per_mshr = 12
    compressor = DeltaCacheCompressor()
    # Explicitly use CompressedTags with no extra fluff
    tags = CompressedTags()

# -------------------------
# Build the system
# -------------------------
system = System()

# Clock domain
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = clk
system.clk_domain.voltage_domain = VoltageDomain()

# Memory mode and range
system.mem_mode = "timing"
system.mem_ranges = [AddrRange("8192MiB")]

# CPU
system.cpu = MyMinorCPU(options=args)

# L1 caches
system.cpu.icache = L1ICache(args)
system.cpu.dcache = L1DCache(args)

system.cpu.icache.connectCPU(system.cpu)
system.cpu.dcache.connectCPU(system.cpu)

# Buses
system.l2bus = L2XBar()
system.l3bus = L2XBar()
system.membus = SystemXBar()

# Hook L1s to L2 bus
system.cpu.icache.connectBus(system.l2bus)
system.cpu.dcache.connectBus(system.l2bus)

# L2 cache
system.l2cache = L2Cache(args)
system.l2cache.connectCPUSideBus(system.l2bus)
system.l2cache.connectMemSideBus(system.l3bus)

# L3 cache (LLC)
system.l3cache = L3Cache(args)
system.l3cache.connectCPUSideBus(system.l3bus)
system.l3cache.connectMemSideBus(system.membus)

# Interrupt controller
system.cpu.createInterruptController()
system.cpu.interrupts[0].pio = system.membus.mem_side_ports
system.cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
system.cpu.interrupts[0].int_responder = system.membus.mem_side_ports

# System port
system.system_port = system.membus.cpu_side_ports

# Memory controller
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = HBM_2000_4H_1x64()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# -------------------------
# ⭐ SE MODE WORKLOAD ⭐
# -------------------------
system.workload = SEWorkload.init_compatible(binary)

process = Process()
process.cmd = [binary] + args.options.split()
system.cpu.workload = process
system.cpu.createThreads()

# Root and run
root = Root(full_system=False, system=system)
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")

# -------------------------
# LLC dump (once BaseCache::dumpCacheLines exists)
# -------------------------
# with open("llc_dump.txt", "w") as f:
#     system.l3cache.dumpCacheLines(f)
