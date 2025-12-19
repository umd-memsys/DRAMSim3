#!/bin/bash

# --- 1. Check for Command ---
# Exit if no command is given
if [ $# -eq 0 ]; then
    echo "Usage: $0 <command-to-run>"
    echo "Example: $0 sde -debug-trace -- ./myprogram"
    exit 1
fi

# --- 2. Define Job Details ---
# Use the first argument (e.g., "sde") as the job name
JOB_NAME="$1"
# Use all arguments as the full command to run
COMMAND_TO_RUN="$@"

# --- 3. Submit to SLURM ---
# Use 'sbatch' and pipe the script to it using a "here document" (<< EOF)
sbatch << EOF
#!/bin/bash

# --- SLURM DIRECTIVES ---
# These are reasonable defaults for a quick, non-GPU job
# based on the UVA CS documentation.

#SBATCH -J "${JOB_NAME}"          # Job name (e.g., "sde")
#SBATCH -p cpu                      # Partition: Use the general 'cpu' partition
#SBATCH -n 1                        # Number of tasks (1)
#SBATCH -c 4                        # CPUs per task (4)
#SBATCH --mem=16000                 # Memory per node (16GB)
#SBATCH -t 96:00:00                 # Time limit (12 hours)

# Output files: e.g., sde-123456.out, sde-123456.err
# %A is the Job ID
#SBATCH --output="${JOB_NAME}-%A.out"
#SBATCH --error="${JOB_NAME}-%A.err"


# --- JOB EXECUTION ---
echo "================================================="
echo "SLURM Job     : \$SLURM_JOB_ID"
echo "Job Name      : \$SLURM_JOB_NAME"
echo "Host          : \$(hostname)"
echo "Start Time    : \$(date)"
echo "Work Directory: \$(pwd)"
echo "================================================="
echo ""
echo "Running command:"
echo "${COMMAND_TO_RUN}"
echo ""

# Execute the command passed to the script
${COMMAND_TO_RUN}

echo ""
echo "================================================="
echo "End Time: \$(date)"
echo "================================================="
EOF

# Print confirmation to the terminal
echo "Submitted job '${JOB_NAME}' to the 'cpu' partition."
