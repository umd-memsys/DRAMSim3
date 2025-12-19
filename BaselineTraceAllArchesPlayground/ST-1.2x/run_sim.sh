#!/bin/bash
# Slurm directives (optional defaults if used with sbatch)
#SBATCH --job-name=ST-1.2x
#SBATCH --partition=cpu
#SBATCH --mem=8G
#SBATCH --output=slurm_%j.out
#SBATCH --error=slurm_%j.err

echo 'Starting Simulation...'
/bigtemp/jrs6qe/DepCompProject/DRAMsim3/build/dramsim3main /bigtemp/jrs6qe/DepCompProject/DRAMsim3/configs/ST-1.2x.ini --stream random -c 100000 
echo 'Simulation Complete'
