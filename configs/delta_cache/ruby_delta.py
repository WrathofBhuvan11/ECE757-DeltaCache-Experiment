import argparse
import os

import m5
from m5.objects import *
from m5.util import addToPath

# Add common gem5 script paths
addToPath("../")
from common import (
    Options,
    Simulation,
)
from ruby import MESI_Three_Level

# -------------------------------------------------------------------------
# 1. System Configuration
# -------------------------------------------------------------------------
system = System()
system.clk_domain = SrcClockDomain(
    clock="2GHz", voltage_domain=VoltageDomain()
)
system.mem_mode = "timing"
system.mem_ranges = [AddrRange("2GiB")]

# -------------------------------------------------------------------------
# 2. CPU Setup (X86 Timing)
# -------------------------------------------------------------------------
system.cpu = X86TimingSimpleCPU()
system.cpu.createInterruptController()

# -------------------------------------------------------------------------
# 3. Ruby and Protocol Options
# -------------------------------------------------------------------------
system.ruby = RubySystem()

parser = argparse.ArgumentParser()
Options.addCommonOptions(parser)
Options.addSEOptions(parser)
MESI_Three_Level.define_options(parser)

# Use defaults for the protocol
args = parser.parse_args([])
args.num_cpus = 1
args.num_l3caches = 1
args.num_dirs = 1
args.topology = "crossbar"


# 4. Initialize Ruby
cpu_ports = []
for name in ["icache_port", "dcache_port"]:
    if hasattr(system.cpu, name):
        cpu_ports.append(getattr(system.cpu, name))

walker_names = ["itb_walker_cache_port", "dtb_walker_cache_port"]
for name in walker_names:
    if hasattr(system.cpu, name):
        cpu_ports.append(getattr(system.cpu, name))
    elif hasattr(system.cpu, "mmu") and hasattr(system.cpu.mmu, name):
        cpu_ports.append(getattr(system.cpu.mmu, name))

# ⭐ REMOVED clk_domain HERE ⭐
MESI_Three_Level.create_system(
    args, full_system=False, system=system, cpu_ports=cpu_ports
)

# -------------------------------------------------------------------------
# 5. Hook the Delta Compressor to Ruby L3
# -------------------------------------------------------------------------
# In MESI_Three_Level, the Shared L3 is found in the l3_cntrl list
# We attach our compressor to each L3 bank's CacheMemory object
l3_cntrls = getattr(
    system.ruby, "l3_cntrl", getattr(system.ruby, "l3_cntrls", [])
)
if not isinstance(l3_cntrls, list):
    l3_cntrls = [l3_cntrls]

for cntrl in l3_cntrls:
    cntrl.L3cache.compressor = DeltaCacheCompressor()

# -------------------------------------------------------------------------
# 6. Workload Setup (DAXPY)
# -------------------------------------------------------------------------
# Ensure this path is correct relative to your gem5 root!
binary = "configs/delta_cache/daxpy"
system.workload = SEWorkload.init_compatible(binary)

process = Process()
process.executable = binary
process.cmd = [binary]
system.cpu.workload = process
system.cpu.createThreads()

# -------------------------------------------------------------------------
# 7. Memory Controllers
# -------------------------------------------------------------------------
# Ruby provides its own memory output ports that connect to the MemCtrl
system.mem_ctrls = [
    MemCtrl(dram=DDR3_1600_8x8(range=system.mem_ranges[0]), port=p)
    for p in system.ruby.memory_out_port
]

# -------------------------------------------------------------------------
# 8. Run Simulation
# -------------------------------------------------------------------------
root = Root(full_system=False, system=system)
m5.instantiate()

print("Beginning Ruby simulation with DeltaCache Matching (MESI_Three_Level)!")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
