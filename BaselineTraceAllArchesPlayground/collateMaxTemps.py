import os
import re
import csv
from pathlib import Path

# ==========================================
# CONFIGURATION
# ==========================================

SUMMARY_FILENAME = "slurm_summary.csv"

# Regex to find: "MaxT of case 1 is 54.5326 [C]"
# We capture the number group (\d+\.\d+)
TARGET_LINE_PATTERN = re.compile(r"MaxT of case \d+ is\s+([0-9.]+)\s+\[C\]")

# ==========================================
# FUNCTIONS
# ==========================================

def get_latest_slurm_file(folder_path):
    """
    Finds the most recently modified slurm_*.out file in a folder.
    This ensures that if you ran a job twice, we grab the result from the latest run.
    """
    slurm_files = list(folder_path.glob("slurm_*.out"))
    if not slurm_files:
        return None
    
    # Sort by modification time, newest last
    slurm_files.sort(key=lambda f: f.stat().st_mtime)
    return slurm_files[-1]

def extract_temp_from_file(file_path):
    """Reads the file line by line and looks for the MaxT regex match."""
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                match = TARGET_LINE_PATTERN.search(line)
                if match:
                    # Return the captured number as a float
                    return float(match.group(1))
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    return None

def main():
    root_dir = Path.cwd()
    results = []

    print(f"Scanning subdirectories in {root_dir} for Slurm logs...")

    # Iterate over all items in the current directory
    # We look for folders that act as config containers
    found_count = 0
    
    for item in root_dir.iterdir():
        if item.is_dir():
            # 1. Find the target log file
            slurm_file = get_latest_slurm_file(item)
            
            if slurm_file:
                # 2. Extract data
                temp = extract_temp_from_file(slurm_file)
                
                if temp is not None:
                    print(f"Found: {item.name} -> {temp} (from {slurm_file.name})")
                    results.append((item.name, temp))
                    found_count += 1
                else:
                    # Log file exists, but the specific "MaxT" line wasn't found (job failed or incomplete)
                    print(f"Skipping {item.name}: 'MaxT' pattern not found in {slurm_file.name}")
            else:
                # Just a random folder, or a config folder that hasn't run yet. Silent skip.
                pass

    # 3. Write results
    if results:
        # Sort alphabetically by config name
        results.sort(key=lambda x: x[0])
        
        with open(SUMMARY_FILENAME, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(["Config", "Max_Temperature"])
            writer.writerows(results)
        
        print(f"\n--- Processing Complete ---")
        print(f"Successfully extracted {found_count} results.")
        print(f"Saved to: {SUMMARY_FILENAME}")
    else:
        print("\nNo matching simulation results found in any subdirectory.")

if __name__ == "__main__":
    main()