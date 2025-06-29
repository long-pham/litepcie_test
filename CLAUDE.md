# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview
This is a LiteX/LitePCIe FPGA development project designed for remote development on Ubuntu systems. The project uses Python for host-side tooling and interfaces with FPGA hardware via PCIe.

## Commands

### Development Setup
```bash
# Install dependencies using UV (preferred)
uv pip install -e .

# Or using standard pip
pip install -e .

# Install LiteX dependencies (on remote Ubuntu system)
pip install litex litepcie
```

### Running the Application
```bash
# After installation, run the CLI application
litepcie-test

# Common LiteX commands (on remote system)
litepcie_util info       # Show PCIe device info
litepcie_util dma_test   # Run DMA tests
```

### File Synchronization (for FPGA development)
```bash
# Start synchronization to remote FPGA system
python scripts/mutagen_sync.py start

# Check synchronization status
python scripts/mutagen_sync.py status

# Stop synchronization
python scripts/mutagen_sync.py stop

# Monitor sync in real-time
python scripts/mutagen_sync.py monitor
```

### Testing
```bash
# Run unit tests
pytest tests/

# Run tests with coverage
pytest --cov=litepcie_test tests/

# Run specific test file
pytest tests/test_specific.py
```

### Code Quality
```bash
# Format code with black
black src/ tests/

# Lint with ruff
ruff check src/ tests/

# Type check with mypy
mypy src/
```

## Architecture Overview

### Project Structure
This is a Python CLI application for testing LiteX/LitePCIe FPGA interfaces. The codebase uses modern Python packaging (pyproject.toml) with UV as the package manager.

### Key Components

1. **Main Application** (`src/litepcie_test/`)
   - Entry point through `__main__.py` 
   - CLI interface built with Click framework
   - Logging handled by Loguru
   - Interfaces with LitePCIe drivers on remote system

2. **Synchronization System** (`scripts/mutagen_sync.py`)
   - Manages bidirectional file sync between local development and remote FPGA systems
   - Configured via `mutagen-sync.yml`
   - Default remote: `rem@192.168.1.67`
   - Respects `.gitignore` patterns for exclusions

3. **FPGA Integration**
   - Communicates with LiteX SoC via PCIe interface
   - Uses LitePCIe Python bindings for DMA transfers
   - Supports memory-mapped register access

### Development Workflow
The project is designed for remote FPGA development:
1. Code is developed locally (macOS/Windows/Linux)
2. Files are synchronized to remote Ubuntu system using Mutagen
3. FPGA bitstream is loaded on remote system
4. Testing happens on remote system with FPGA hardware
5. Results are synchronized back to local system

### Remote System Requirements
- Ubuntu 20.04+ with PCIe-capable FPGA board
- LiteX/LitePCIe installed
- Python 3.10.11
- FPGA programmed with compatible LitePCIe design

### Dependencies
- Python 3.10.11 required (exact version specified in pyproject.toml)
- Key packages: click (CLI), loguru (logging), pyyaml (configuration)
- UV package manager for dependency management
- Remote: litex, litepcie, migen (FPGA toolchain)