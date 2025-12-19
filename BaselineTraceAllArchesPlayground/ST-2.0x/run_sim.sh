#!/bin/bash
# Slurm directives (optional defaults if used with sbatch)
#SBATCH --job-name=ST-2.0x
#SBATCH --partition=cpu
#SBATCH --mem=8G
#SBATCH --output=slurm_%j.out
#SBATCH --error=slurm_%j.err

echo 'Starting Simulation...'
/bigtemp/jrs6qe/DepCompProject/DRAMsim3/build/dramsim3main /bigtemp/jrs6qe/DepCompProject/DRAMsim3/configs/ST-2.0x.ini --stream random -c 100000 
echo 'Simulation Complete'
