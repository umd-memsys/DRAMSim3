import os

filename = "hbm_thermal_attack.trace"
num_accesses = 200000

# HBM TIMING CALCULATION
# tRC = tRAS (34) + tRP (14) = 48 cycles.
# We want to issue commands exactly as fast as the bank can cycle.
cycle_step = 48

# ADDRESS TARGETING (Mapping: rorabgbachco)
# To hit the SAME BANK (hotspot) but DIFFERENT ROW (force ACT/PRE energy):
# We need to toggle the MSBs (Row bits) while keeping the LSBs (Bank/Chan) identical.
# 0x10000000 guarantees we flip a high Row bit.
addr_A = "0x00000000"
addr_B = "0x10000000"

with open(filename, "w") as f:
    current_cycle = 10
    for i in range(num_accesses):
        # 1. Toggle Address to force Row Miss (ACT + PRE energy)
        addr = addr_A if i % 2 == 0 else addr_B
        
        # 2. Use WRITE commands
        # Your Config: IDD4W (500) > IDD4R (390)
        # Writes are 28% hotter than Reads on this HBM chip.
        cmd = "WRITE"
        
        f.write(f"{addr} {cmd} {current_cycle}\n")
        
        # 3. Increment strictly by tRC to saturate the bank
        current_cycle += cycle_step

print(f"Generated {filename}")
print("Run this trace with DRAMsim3 compiled with -DTHERMAL=1")