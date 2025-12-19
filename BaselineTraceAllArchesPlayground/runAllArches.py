import os
import sys
import glob
import subprocess
import csv
import shutil
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor

# ==========================================
# CONFIGURATION
# ==========================================

# Set this to True to submit to Slurm (sbatch), False to run locally on this machine
USE_SLURM = True

# Slurm Partition (Queue) Name - REQUIRED if USE_SLURM is True
# Run 'sinfo' in your terminal to see available partitions (e.g., 'batch', 'compute', 'gpu')
SLURM_PARTITION = "cpu" 

# Maximum concurrent processes if running locally (None = number of CPU cores)
MAX_WORKERS = 90 

# Paths (Relative to where this script is run)
# Using pathlib to resolve absolute paths prevents "directory hell"
SCRIPT_DIR = Path.cwd()
CONFIG_DIR = (SCRIPT_DIR / "../configs").resolve()
BUILD_EXE  = (SCRIPT_DIR / "../build/dramsim3main").resolve()
TRACE_FILE = (SCRIPT_DIR / "../generateTracePlayground/thermal_attack.trace").resolve()

# Output filename for the collated results
SUMMARY_CSV = "final_summary.csv"

# ==========================================
# FUNCTIONS
# ==========================================

def get_config_files():
    """Finds all .ini files in the config directory."""
    if not CONFIG_DIR.exists():
        print(f"Error: Config directory not found at {CONFIG_DIR}")
        sys.exit(1)
    
    # Get all .ini files
    files = list(CONFIG_DIR.glob("*.ini"))
    if not files:
        print(f"No .ini files found in {CONFIG_DIR}")
        sys.exit(1)
    
    print(f"Found {len(files)} config files.")
    return files

def setup_simulation_folder(config_path):
    """
    Creates the folder and the shell script for a single simulation.
    Returns: (folder_name, path_to_shell_script)
    """
    config_name = config_path.stem # e.g., "GDDR5_32Gb"
    
    # 1. Create directory
    work_dir = SCRIPT_DIR / config_name
    work_dir.mkdir(exist_ok=True)
    
    # 2. Construct the command
    # We use absolute paths in the script to ensure it runs regardless of where it's called
    # However, we preserve the user's specific logic regarding arguments
    cmd = (
        f"{BUILD_EXE} "
        f"{config_path} "
        f"--stream "
        f"random "
        f"-c 100000 "
    )
    
    script_path = work_dir / "run_sim.sh"
    
    # Write the shell script
    with open(script_path, "w") as f:
        f.write("#!/bin/bash\n")
        f.write("# Slurm directives (optional defaults if used with sbatch)\n")
        f.write(f"#SBATCH --job-name={config_name}\n")
        f.write(f"#SBATCH --partition={SLURM_PARTITION}\n")
        f.write("#SBATCH --mem=8G\n")  # Request 8GB of RAM (adjust as needed)
        f.write("#SBATCH --output=slurm_%j.out\n")
        f.write("#SBATCH --error=slurm_%j.err\n")
        f.write("\n")
        f.write("echo 'Starting Simulation...'\n")
        f.write(f"{cmd}\n")
        f.write("echo 'Simulation Complete'\n")
    
    # Make script executable
    script_path.chmod(0o755)
    
    return config_name, work_dir, script_path

def run_local_process(args):
    """Wrapper to run the script inside its specific folder."""
    config_name, work_dir, script_path = args
    
    print(f"[{config_name}] Starting...")
    
    try:
        # Run the generated shell script inside its own directory
        # capture_output=True keeps the terminal clean, storing logs in result
        result = subprocess.run(
            ["./run_sim.sh"], 
            cwd=work_dir, 
            capture_output=True, 
            text=True
        )
        
        if result.returncode == 0:
            print(f"[{config_name}] Finished successfully.")
            return True
        else:
            print(f"[{config_name}] FAILED. Return code: {result.returncode}")
            # Write stderr to a log file for debugging
            with open(work_dir / "error.log", "w") as f:
                f.write(result.stderr)
            return False
            
    except Exception as e:
        print(f"[{config_name}] Exception: {e}")
        return False

def submit_slurm_job(args):
    """Submits the generated script to slurm."""
    config_name, work_dir, script_path = args
    
    print(f"[{config_name}] Submitting to Slurm...")
    try:
        subprocess.run(["sbatch", "run_sim.sh"], cwd=work_dir, check=True)
        return True
    except subprocess.CalledProcessError:
        print(f"[{config_name}] Failed to submit sbatch.")
        return False

def collect_results(configs_map):
    """
    Iterates through folders and parses the CSV.
    configs_map is a list of tuples: (config_name, work_dir, script_path)
    """
    print("\nCollecting results...")
    results = []
    
    for config_name, work_dir, _ in configs_map:
        result_file = work_dir / "dramsim3final_temp.csv"
        
        if not result_file.exists():
            print(f"[{config_name}] Warning: No result CSV found.")
            continue
            
        try:
            with open(result_file, 'r') as f:
                reader = csv.reader(f)
                # Format expected:
                # rank_channel_index,x,y,z,power,temperature
                # 0,0,0,0,0.822309,84.831
                
                header = next(reader, None) # Skip header
                data_row = next(reader, None) # Get first data row
                
                if data_row:
                    # Parse the last column (temperature)
                    temp_str = data_row[-1].strip()
                    temp_val = float(temp_str)
                    results.append((config_name, temp_val))
                else:
                    print(f"[{config_name}] CSV file was empty.")
                    
        except Exception as e:
            print(f"[{config_name}] Error parsing CSV: {e}")

    # Write summary to file
    with open(SUMMARY_CSV, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(["Config", "Max_Temperature"])
        for name, temp in results:
            writer.writerow([name, temp])
            
    print(f"\nDone! Summary saved to: {SUMMARY_CSV}")
    # Print preview
    for r in results[:5]:
        print(f"{r[0]}: {r[1]}")
    if len(results) > 5: print("...")

# ==========================================
# MAIN EXECUTION
# ==========================================

def main():
    print("--- DramSim3 Automation Manager ---")
    
    # 1. Identify Configs
    config_files = get_config_files()
    
    # 2. Prepare Directories and Scripts
    simulation_tasks = []
    for cfg in config_files:
        task_info = setup_simulation_folder(cfg)
        simulation_tasks.append(task_info)
    
    # 3. Execute Simulations
    if USE_SLURM:
        print("Mode: Slurm Submission")
        for task in simulation_tasks:
            submit_slurm_job(task)
        print("\nAll jobs submitted. Run this script again strictly for collection once jobs finish,")
        print("or ensure you wait for jobs to complete.")
        # We exit here because Slurm is async. We can't collect immediately.
        return 

    else:
        print(f"Mode: Local Parallel Execution (Max Workers: {MAX_WORKERS if MAX_WORKERS else 'Auto'})")
        
        # use ProcessPoolExecutor to run simulations in parallel
        with ProcessPoolExecutor(max_workers=MAX_WORKERS) as executor:
            # We map the function run_local_process to the list of tasks
            results = list(executor.map(run_local_process, simulation_tasks))
            
        print("\nAll local simulations finished.")
        
        # 4. Collect Data (Only done immediately if running locally)
        collect_results(simulation_tasks)

if __name__ == "__main__":
    main()