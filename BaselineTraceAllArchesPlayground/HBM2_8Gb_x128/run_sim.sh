#!/bin/bash
# Slurm directives (optional defaults if used with sbatch)
#SBATCH --job-name=HBM2_8Gb_x128
#SBATCH --partition=cpu
#SBATCH --mem=8G
#SBATCH --output=slurm_%j.out
#SBATCH --error=slurm_%j.err

echo 'Starting Simulation...'
/bigtemp/jrs6qe/DepCompProject/DRAMsim3/build/dramsim3main /bigtemp/jrs6qe/DepCompProject/DRAMsim3/configs/HBM2_8Gb_x128.ini --stream random -c 100000 
echo 'Simulation Complete'
