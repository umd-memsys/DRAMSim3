#!/bin/bash
# Slurm directives (optional defaults if used with sbatch)
#SBATCH --job-name=DDR4_4Gb_x8_2400_2
#SBATCH --partition=cpu
#SBATCH --mem=8G
#SBATCH --output=slurm_%j.out
#SBATCH --error=slurm_%j.err

echo 'Starting Simulation...'
/bigtemp/jrs6qe/DepCompProject/DRAMsim3/build/dramsim3main /bigtemp/jrs6qe/DepCompProject/DRAMsim3/configs/DDR4_4Gb_x8_2400_2.ini --stream random -c 100000 
echo 'Simulation Complete'
