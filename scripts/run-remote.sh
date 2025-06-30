#!/bin/bash
# Wrapper script to execute commands on the remote Ubuntu server.

# --- Configuration ---
REMOTE_USER="rem"
REMOTE_HOST="192.168.1.67"
REMOTE_PROJECT_DIR="/Volumes/ss990Pro2T/githublp/litepcie_test"
# ---------------------

if [ -z "$1" ]; then
  echo "Usage: $0 \"command to execute\""
  exit 1
fi

COMMAND=$1

# Execute the command on the remote server in the project directory
ssh "${REMOTE_USER}@${REMOTE_HOST}" "cd ${REMOTE_PROJECT_DIR} && source .venv/bin/activate && ${COMMAND}"
