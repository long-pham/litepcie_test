
# Gemini Project Configuration: litepcie_test

This document outlines the specific workflow for this project, which involves local editing on macOS and running tasks on a remote Ubuntu server.

## Remote Execution

To run any command (like `pytest`, or a build script) on the remote machine, use the `scripts/run-remote.sh` wrapper.

**Usage:**

```bash
./scripts/run-remote.sh "your command here"
```

## File Synchronization

File changes are synchronized between the local machine and the remote server using Mutagen, as configured in `mutagen-sync.yml`.

- To start the sync, run: `mutagen-compose -f scripts/mutagen-compose.yml up`
- To stop the sync, run: `mutagen-compose -f scripts/mutagen-compose.yml down`

This is managed by the `scripts/mutagen_sync.py` script.
