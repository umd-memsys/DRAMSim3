#!/bin/bash
# Slurm directives (optional defaults if used with sbatch)
#SBATCH --job-name=DDR4_8Gb_x4_2933
#SBATCH --partition=cpu
#SBATCH --mem=8G
#SBATCH --output=slurm_%j.out
#SBATCH --error=slurm_%j.err

echo 'Starting Simulation...'
/bigtemp/jrs6qe/DepCompProject/DRAMsim3/build/dramsim3main /bigtemp/jrs6qe/DepCompProject/DRAMsim3/configs/DDR4_8Gb_x4_2933.ini -c 100000 -t /bigtemp/jrs6qe/DepCompProject/DRAMsim3/generateTracePlayground/thermal_attack.trace
echo 'Simulation Complete'
