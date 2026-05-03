import argparse
import os
import sys
import shlex

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import addToPath

addToPath("../")

from common import Options
from ruby import Ruby

config_path = os.path.dirname(os.path.abspath(__file__))
config_root = os.path.dirname(config_path)

parser = argparse.ArgumentParser()
Options.addCommonOptions(parser)
Options.addSEOptions(parser)
Ruby.define_options(parser)

parser.add_argument(
    "--binary",
    required=True,
    help="path to SE-mode binary to run on the simulated CPU",
)
parser.add_argument(
    "--binary-args",
    default="",
    help="quoted argv string passed to the binary (e.g. \"100 0.5\")",
)

args = parser.parse_args()
args.protocol = buildEnv["PROTOCOL"]

if args.protocol != "DeltaCache":
    fatal(f"This script requires PROTOCOL=DeltaCache (got {args.protocol})")

if not os.path.isfile(args.binary):
    fatal(f"binary not found: {args.binary}")

# Cache sizes — match verify_map_table.py defaults
args.l1d_size = "32KiB"
args.l1i_size = "32KiB"
args.l2_size  = "256KiB"
args.l3_size  = "1MB"
args.l1d_assoc = 2
args.l1i_assoc = 2
args.l2_assoc  = 8
args.l3_assoc  = 16

# -----------------------------------------------------
# System + CPU
# -----------------------------------------------------
system = System(
    cpu=[X86TimingSimpleCPU(cpu_id=i) for i in range(args.num_cpus)],
    mem_ranges=[AddrRange(args.mem_size)],
    mem_mode="timing",
)

system.voltage_domain = VoltageDomain(voltage=args.sys_voltage)
system.clk_domain = SrcClockDomain(
    clock=args.sys_clock, voltage_domain=system.voltage_domain
)

# -----------------------------------------------------
# SE-mode workload
# -----------------------------------------------------
system.workload = SEWorkload.init_compatible(args.binary)

argv = [args.binary] + shlex.split(args.binary_args)
process = Process(executable=args.binary, cmd=argv)
for cpu in system.cpu:
    cpu.workload = process
    cpu.createThreads()

# -----------------------------------------------------
# Ruby
# -----------------------------------------------------
Ruby.create_system(args, False, system, cpus=system.cpu)
system.ruby.clk_domain = SrcClockDomain(
    clock=args.ruby_clock, voltage_domain=system.voltage_domain
)

# Sequencer-side hookup. connectCpuPorts handles X86 interrupt ports too.
assert len(system.ruby._cpu_ports) == args.num_cpus
for i, ruby_port in enumerate(system.ruby._cpu_ports):
    system.cpu[i].createInterruptController()
    ruby_port.connectCpuPorts(system.cpu[i])
    ruby_port.no_retry_on_stall = True

# -----------------------------------------------------
# DeltaMapTable wiring (mirrors verify_map_table.py)
# -----------------------------------------------------
map_table = DeltaMapTable(table_entries=1024)

controllers = []
idx = 0
while hasattr(system.ruby, f"l2_cntrl{idx}"):
    controllers.append(getattr(system.ruby, f"l2_cntrl{idx}"))
    idx += 1

if not controllers:
    fatal("Could not find any L2 controllers to attach DeltaMapTable!")

print(f"Connecting DeltaMapTable to {len(controllers)} L2 controller(s)...")
for l2_cntrl in controllers:
    l2_cntrl.mapTable = map_table

# -----------------------------------------------------
# Run
# -----------------------------------------------------
root = Root(full_system=False, system=system)
m5.ticks.setGlobalFrequency("1ns")
m5.instantiate()

print(f"Running {args.binary} {args.binary_args}")
exit_event = m5.simulate(args.abs_max_tick)
print("Exiting @ tick", m5.curTick(), "because", exit_event.getCause())
