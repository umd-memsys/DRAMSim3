filename = "thermal_attack.trace"
num_accesses = 100000

cycle_step = 9

# Addresses mapping to SAME BANK, DIFFERENT ROW
# With 128 columns (7 bits), 4 banks (2 bits), 4 bankgroups (2 bits),
addr_A = "0x00000000" 
addr_B = "0x01000000" 

with open(filename, "w") as f:
    current_cycle = 10
    for i in range(num_accesses):
        # Alternate addresses to force "Row Misses"
        addr = addr_A if i % 2 == 0 else addr_B
        
        # Use READ only
        cmd = "READ"
        
        f.write(f"{addr} {cmd} {current_cycle}\n")
        current_cycle += cycle_step

print(f"Generated {filename} with {num_accesses} accesses.")
print(f"Theoretical Attack Duration: {current_cycle} cycles")