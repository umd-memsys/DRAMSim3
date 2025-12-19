#!/bin/bash
# Slurm directives (optional defaults if used with sbatch)
#SBATCH --job-name=DDR4_4Gb_x16_2666_2
#SBATCH --partition=cpu
#SBATCH --mem=8G
#SBATCH --output=slurm_%j.out
#SBATCH --error=slurm_%j.err

echo 'Starting Simulation...'
/bigtemp/jrs6qe/DepCompProject/DRAMsim3/build/dramsim3main /bigtemp/jrs6qe/DepCompProject/DRAMsim3/configs/DDR4_4Gb_x16_2666_2.ini -c 100000 -t /bigtemp/jrs6qe/DepCompProject/DRAMsim3/generateTracePlayground/thermal_attack.trace
echo 'Simulation Complete'
