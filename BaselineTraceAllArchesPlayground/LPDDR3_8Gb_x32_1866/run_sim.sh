#!/bin/bash
# Slurm directives (optional defaults if used with sbatch)
#SBATCH --job-name=LPDDR3_8Gb_x32_1866
#SBATCH --partition=cpu
#SBATCH --mem=8G
#SBATCH --output=slurm_%j.out
#SBATCH --error=slurm_%j.err

echo 'Starting Simulation...'
/bigtemp/jrs6qe/DepCompProject/DRAMsim3/build/dramsim3main /bigtemp/jrs6qe/DepCompProject/DRAMsim3/configs/LPDDR3_8Gb_x32_1866.ini --stream random -c 100000 
echo 'Simulation Complete'
