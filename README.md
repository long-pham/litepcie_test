# litepcie-test

A Python test framework for LiteX/LitePCIe FPGA development on remote systems.

## Features

- Remote FPGA testing via PCIe interface
- Automated file synchronization with Mutagen
- DMA transfer testing and benchmarking
- Register access utilities
- CLI interface for easy operation

## Requirements

- Python 3.10.11
- Ubuntu 20.04+ (on remote FPGA system)
- LiteX/LitePCIe compatible FPGA board
- SSH access to remote system

## Installation

### Local Development Machine

```bash
# Clone the repository
git clone https://github.com/yourusername/litepcie_test.git
cd litepcie_test

# Install with uv (recommended)
uv pip install -e ".[development]"

# Or with pip
pip install -e ".[development]"
```

### Remote FPGA System

```bash
# Install LiteX toolchain
wget https://raw.githubusercontent.com/enjoy-digital/litex/master/litex_setup.py
python3 litex_setup.py --init --install

# Install this package
pip install litepcie-test
```

## Quick Start

1. Configure your remote host in `mutagen-sync.yml`:
   ```yaml
   profiles:
     default:
       remote_host: user@your-fpga-host
       remote_path: /path/to/remote/project
   ```

2. Start file synchronization:
   ```bash
   python scripts/mutagen_sync.py start
   ```

3. Run tests on the remote system:
   ```bash
   litepcie-test
   ```

## Usage

### Basic Commands

```bash
# Show PCIe device information
litepcie-test info

# Run DMA transfer tests
litepcie-test dma-test --size 1M --iterations 100

# Access registers
litepcie-test reg read 0x0000
litepcie-test reg write 0x0000 0x1234
```

### File Synchronization

The project includes a Mutagen-based sync manager for efficient development:

```bash
# Start sync
python scripts/mutagen_sync.py start

# Monitor sync status
python scripts/mutagen_sync.py status --watch

# Stop sync
python scripts/mutagen_sync.py stop
```

## Development

See [DEVELOPMENT.md](DEVELOPMENT.md) for detailed development workflow, CI/CD setup, and contribution guidelines.

### Running Tests

```bash
# Run all tests
pytest

# Run with coverage
pytest --cov=litepcie_test

# Run specific test category
pytest tests/unit/
```

### Code Quality

```bash
# Format code
ruff format .

# Lint
ruff check .

# Type check
mypy src/
```

## Project Structure

```
litepcie_test/
├── src/litepcie_test/     # Main package
│   ├── __init__.py
│   ├── __main__.py        # CLI entry point
│   ├── core/              # Core functionality
│   ├── drivers/           # Hardware drivers
│   └── cli/               # CLI commands
├── tests/                 # Test suite
├── scripts/               # Utility scripts
│   └── mutagen_sync.py    # Remote sync manager
└── docs/                  # Documentation
```

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please see our [Contributing Guidelines](CONTRIBUTING.md) (coming soon).

## Support

- Issues: [GitHub Issues](https://github.com/yourusername/litepcie_test/issues)
- Documentation: [Wiki](https://github.com/yourusername/litepcie_test/wiki)