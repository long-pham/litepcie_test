# LitePCIe DMA Latency Test Utilities

This directory contains specialized tools for measuring DMA latency and performance characteristics of LitePCIe devices.

## Overview

Two new DMA latency test utilities have been added to complement the existing test suite:

1. **litepcie_dma_latency_test** - Comprehensive DMA performance and latency analysis
2. **litepcie_dma_latency_simple** - Lightweight DMA round-trip latency measurement

## Test Programs

### 1. Comprehensive DMA Latency Test (`litepcie_dma_latency_test`)

A full-featured DMA performance testing tool that provides detailed latency statistics and throughput measurements.

#### Features
- Measures individual DMA operation timings (setup, transfer, completion)
- Calculates statistical distributions (min, max, mean, percentiles)
- Supports configurable buffer sizes and counts
- Provides real-time throughput measurements
- Multi-threaded architecture for accurate timing
- CPU affinity support for consistent results

#### Usage
```bash
./build/litepcie_dma_latency_test [options]

Options:
  -d <device>    Device file (default: /dev/litepcie0)
  -n <count>     Number of transfers (default: 1000)
  -s <size>      Buffer size in bytes (default: 8192)
  -b <buffers>   Number of DMA buffers (default: 32)
  -t <seconds>   Test duration, 0=infinite (default: 0)
  -c <cpu>       CPU affinity (default: auto)
  -p             High priority scheduling
  -v             Verbose output
  -h             Show help
```

#### Example Output
```
LitePCIe DMA Latency Test
========================
Device: /dev/litepcie0
Buffers: 32 x 8192 bytes
Transfers: 10000

DMA Latency Statistics (microseconds):
--------------------------------------
Setup Phase:
  Min:      8.234 µs
  Max:     45.678 µs
  Mean:    12.345 µs
  StdDev:   2.134 µs

Transfer Phase:
  Min:     35.123 µs
  Max:    156.789 µs
  Mean:    45.678 µs
  StdDev:   5.432 µs

Total Round-Trip:
  Min:     43.567 µs
  Max:    198.234 µs
  Mean:    58.901 µs
  StdDev:   7.891 µs

Percentiles (Total):
  50%:     56.789 µs
  90%:     65.432 µs
  95%:     72.345 µs
  99%:     89.123 µs
  99.9%:  145.678 µs

Performance:
  Throughput: 8.456 Gbps
  Transfers/sec: 16,982
```

### 2. Simple DMA Latency Test (`litepcie_dma_latency_simple`)

A minimal-overhead tool focused on measuring basic DMA round-trip latency.

#### Features
- Single-buffer round-trip timing
- Minimal statistical overhead
- Direct hardware timing
- Suitable for quick latency checks
- Low CPU usage

#### Usage
```bash
./build/litepcie_dma_latency_simple [options]

Options:
  -d <device>    Device file (default: /dev/litepcie0)
  -n <count>     Number of iterations (default: 1000)
  -s <size>      Buffer size in bytes (default: 4096)
  -v             Verbose output
  -h             Show help
```

#### Example Output
```
LitePCIe Simple DMA Latency Test
Device: /dev/litepcie0
Buffer: 4096 bytes
Iterations: 1000

Results:
  Min latency:    42.3 µs
  Max latency:   187.6 µs
  Avg latency:    58.7 µs
  Throughput:    557.2 MB/s
```

## Building the Tests

The DMA latency tests are built as part of the standard CMake build process:

```bash
# Build all tests including DMA latency utilities
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# The executables will be in the build directory:
ls -la build/litepcie_dma_latency*
```

## Performance Considerations

### For Accurate Measurements

1. **CPU Governor**: Set to performance mode
   ```bash
   sudo cpupower frequency-set -g performance
   ```

2. **Process Priority**: Run with elevated priority
   ```bash
   sudo nice -n -20 ./build/litepcie_dma_latency_test
   ```

3. **CPU Isolation**: Use isolated CPUs if available
   ```bash
   ./build/litepcie_dma_latency_test -c 3  # Use CPU 3
   ```

### Expected Latencies

Typical DMA round-trip latencies on modern systems:
- **Small buffers (< 4KB)**: 40-80 µs
- **Medium buffers (8-64KB)**: 50-150 µs
- **Large buffers (> 64KB)**: 100-500 µs

Factors affecting latency:
- PCIe generation and width
- System memory bandwidth
- CPU load and interrupts
- DMA engine configuration

## Comparison with Other Tests

| Test Program | Purpose | Overhead | Use Case |
|--------------|---------|----------|----------|
| litepcie_util dma_test | General DMA testing | Medium | Functionality verification |
| litepcie_dma_test_optimized_v2 | Maximum throughput | Low | Performance benchmarking |
| litepcie_dma_latency_test | Detailed latency analysis | Medium | Performance characterization |
| litepcie_dma_latency_simple | Quick latency check | Minimal | Rapid testing |

## Troubleshooting

### High Latency Variance
- Check for CPU throttling: `watch -n 1 'cat /proc/cpuinfo | grep MHz'`
- Monitor interrupts: `watch -n 1 'cat /proc/interrupts | grep litepcie'`
- Disable power saving: `sudo systemctl stop thermald`

### DMA Errors
- Check kernel logs: `dmesg | grep litepcie`
- Verify buffer alignment: Buffers should be page-aligned
- Ensure sufficient DMA buffers in kernel module

### Build Issues
- Ensure kernel headers are installed: `sudo apt install linux-headers-$(uname -r)`
- Check for required libraries: `pkg-config --libs liblitepcie`
- Verify CMake version: `cmake --version` (requires 3.10+)

## Integration with CI/CD

Example test script for automated testing:

```bash
#!/bin/bash
# run_dma_latency_tests.sh

set -e

echo "Running DMA latency tests..."

# Quick sanity check
./build/litepcie_dma_latency_simple -n 100

# Detailed analysis
./build/litepcie_dma_latency_test -n 1000 -v

# Performance regression test
LATENCY=$(./build/litepcie_dma_latency_simple -n 1000 | grep "Avg latency" | awk '{print $3}')
if (( $(echo "$LATENCY > 100" | bc -l) )); then
    echo "WARNING: Average latency ${LATENCY}µs exceeds threshold"
    exit 1
fi

echo "All DMA latency tests passed"
```

## Future Enhancements

Planned improvements for the DMA latency tests:
- Support for scatter-gather DMA operations
- Latency histograms and distribution plots
- Multi-device testing support
- JSON output format for parsing
- Integration with performance monitoring tools