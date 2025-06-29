# Development Workflow for LiteX FPGA Project

## Overview
This guide covers the development workflow for working with LiteX FPGA on remote Ubuntu systems, including CI/CD setup and best practices.

## Local Development Setup

### 1. Initial Setup
```bash
# Clone the repository
git clone <your-repo-url>
cd litepcie_test

# Install dependencies
uv pip install -e ".[dev]"

# Configure remote host
# Edit mutagen-sync.yml with your remote host details
```

### 2. Configure Claude Code Settings
The `.claude/settings.json` file is pre-configured with optimal settings for FPGA development:
- Web search enabled for latest LiteX documentation
- Confirmations for destructive commands
- Python formatting/linting preferences
- FPGA-specific context

### 3. Development Tools
```bash
# Start file synchronization
python scripts/mutagen_sync.py start

# Code formatting
black src/ tests/

# Linting
ruff check src/ tests/

# Type checking
mypy src/

# Run tests locally
pytest tests/
```

## Remote FPGA Development

### 1. Remote System Setup
On your Ubuntu remote system:
```bash
# Install LiteX toolchain
wget https://raw.githubusercontent.com/enjoy-digital/litex/master/litex_setup.py
python3 litex_setup.py --init --install

# Install project
pip install -e .

# Check PCIe device
sudo lspci | grep Xilinx  # or your FPGA vendor
```

### 2. FPGA Programming
```bash
# Load bitstream (example for Xilinx)
openocd -f interface/ftdi/digilent-hs2.cfg -f target/xilinx_7series.cfg -c "init; pld load 0 bitstream.bit; exit"

# Or use Vivado/vendor tools
vivado -mode batch -source program.tcl
```

### 3. Testing on Hardware
```bash
# Check LitePCIe device
sudo litepcie_util info

# Run DMA tests
sudo litepcie_util dma_test

# Run your application
sudo litepcie-test
```

## CI/CD Workflows

### GitHub Actions
The `.github/workflows/ci.yml` provides:
- Python 3.12 testing
- Code quality checks (ruff, black, mypy)
- Unit tests with coverage
- Package building
- Optional LiteX simulation tests

To use:
1. Push to `main` or `develop` branches
2. Create pull requests
3. Tag releases for package builds

### GitLab CI
The `.gitlab-ci.yml` provides:
- Staged pipeline (test → build → deploy)
- Caching for faster builds
- Coverage reporting
- Hardware-in-the-loop tests (manual trigger)
- Internal PyPI deployment

To use:
1. Push to GitLab
2. Use merge requests for testing
3. Tag releases for deployment
4. Manual trigger for hardware tests

### Hardware-in-the-Loop Testing
For GitLab runners with FPGA hardware:
1. Tag runners with `fpga` and `litex`
2. Configure runner with Ubuntu and FPGA access
3. Manual trigger prevents blocking on hardware availability

## Project Structure Best Practices

### 1. Code Organization
```
src/litepcie_test/
├── __init__.py
├── __main__.py         # CLI entry point
├── core/               # Core functionality
│   ├── dma.py         # DMA operations
│   ├── registers.py   # Register access
│   └── utils.py       # Utilities
├── cli/               # CLI commands
│   ├── test.py       # Test commands
│   └── config.py     # Configuration
└── drivers/           # Hardware drivers
    └── pcie.py       # PCIe interface
```

### 2. Testing Structure
```
tests/
├── unit/              # Unit tests (run in CI)
│   ├── test_core.py
│   └── test_cli.py
├── integration/       # Integration tests
│   └── test_sync.py
└── hardware/          # Hardware tests (manual)
    └── test_dma.py
```

### 3. Configuration Files
- `pyproject.toml`: Project metadata and dependencies
- `mutagen-sync.yml`: File synchronization config
- `.claude/settings.json`: Claude Code preferences
- `CLAUDE.md`: Claude Code guidance

## Common Tasks

### Adding New Features
1. Create feature branch
2. Implement with tests
3. Run quality checks locally
4. Push and create PR/MR
5. CI validates changes
6. Test on hardware if needed

### Debugging Remote Issues
```bash
# Check sync status
python scripts/mutagen_sync.py status

# SSH to remote
ssh rem@192.168.1.67

# Check kernel messages
dmesg | grep litepcie

# Monitor PCIe traffic
sudo pcietracer  # if available
```

### Performance Testing
```bash
# DMA bandwidth test
sudo litepcie_util dma_test --size 1M --count 1000

# Latency measurements
sudo litepcie-test benchmark --mode latency
```

## Security Considerations

1. **SSH Keys**: Use SSH keys for remote access, not passwords
2. **PCIe Access**: Requires root/sudo on remote system
3. **Bitstream Security**: Store bitstreams securely, not in git
4. **CI Secrets**: Use CI/CD secret management for credentials

## Troubleshooting

### Sync Issues
```bash
# Reset sync
python scripts/mutagen_sync.py stop
python scripts/mutagen_sync.py start --force

# Check logs
mutagen sync list
mutagen sync monitor <session-id>
```

### FPGA Issues
```bash
# Check device presence
lspci -d <vendor>:<device>

# Reset PCIe device
echo 1 > /sys/bus/pci/devices/<device>/reset

# Reload driver
sudo modprobe -r litepcie
sudo modprobe litepcie
```

### CI/CD Issues
- Check runner logs for hardware tests
- Verify FPGA runner configuration
- Check PyPI credentials for deployment

## Resources

- [LiteX Documentation](https://github.com/enjoy-digital/litex/wiki)
- [LitePCIe Repository](https://github.com/enjoy-digital/litepcie)
- [Mutagen Documentation](https://mutagen.io/documentation)
- [UV Package Manager](https://github.com/astral-sh/uv)