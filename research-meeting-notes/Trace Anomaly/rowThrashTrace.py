filename = "thermal_attack.trace"
num_accesses = 100000

# OPTIMIZATION 1: Sync with Hardware Limits
# tRC = tRAS (56) + tRP (24) = 80 cycles
cycle_step = 9
read_write_swap = 5
read = True

# Addresses mapping to SAME BANK, DIFFERENT ROW
# 0x01000000 sets the 25th bit. 
# With 128 columns (7 bits), 4 banks (2 bits), 4 bankgroups (2 bits),
# The 25th bit is safely in the 'Row' section of the address.
addr_A = "0x00000000" 
addr_B = "0x01000000" 

with open(filename, "w") as f:
    current_cycle = 10
    for i in range(num_accesses):
        # Alternate addresses to force "Row Misses" (Bank Thrashing)
        addr = addr_A if i % 2 == 0 else addr_B
        
        # OPTIMIZATION 2: Use READ only
        # Your config: IDD4R = 248mA vs IDD4W = 231mA
        # Reading generates approx 7% more current than writing in this specific chip.
        cmd = "READ" if read else "WRITE"

        if i % read_write_swap == 0:
            read = not read
        
        f.write(f"{addr} {cmd} {current_cycle}\n")
        current_cycle += cycle_step

print(f"Generated {filename} with {num_accesses} accesses.")
print(f"Theoretical Attack Duration: {current_cycle} cycles")