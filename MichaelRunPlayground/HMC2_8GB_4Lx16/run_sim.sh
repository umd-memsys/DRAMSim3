#!/bin/bash
# Slurm directives (optional defaults if used with sbatch)
#SBATCH --job-name=HMC2_8GB_4Lx16
#SBATCH --partition=cpu
#SBATCH --mem=8G
#SBATCH --output=slurm_%j.out
#SBATCH --error=slurm_%j.err

echo 'Starting Simulation...'
/bigtemp/jrs6qe/DepCompProject/DRAMsim3/build/dramsim3main /bigtemp/jrs6qe/DepCompProject/DRAMsim3/configs/HMC2_8GB_4Lx16.ini -c 100000 -t /bigtemp/jrs6qe/DepCompProject/DRAMsim3/generateTracePlayground/thermal_attack.trace
echo 'Simulation Complete'
