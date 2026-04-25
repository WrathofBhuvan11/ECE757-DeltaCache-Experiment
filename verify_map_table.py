import m5
from m5.objects import *

# 1. Instantiate your new SimObject
# We use the parameters defined in your DeltaMapTable.py
test_table = DeltaMapTable(block_size=64, latency=1)

# 2. Every gem5 simulation needs a Root object
root = Root(full_system=False, test_obj=test_table)

# 3. Instantiate the C++ objects
# This is the line that actually calls your C++ constructor!
m5.instantiate()

print("Python: m5.instantiate() completed successfully!")
