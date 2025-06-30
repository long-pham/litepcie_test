# LitePCIe DMA Test - Complete Build and Usage Guide

This guide provides instructions for building and running the optimized LitePCIe DMA test utilities, including performance tuning and troubleshooting.

## Table of Contents
- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Building the Tests](#building-the-tests)
- [Running the Tests](#running-the-tests)
- [Performance Optimization](#performance-optimization)
- [Troubleshooting](#troubleshooting)
- [Understanding the Results](#understanding-the-results)

## Overview

We have three versions of the DMA test:
1. **Original** - The standard LitePCIe utility (`litepcie_util`)
2. **Optimized V1** - Multi-threaded but with TX bottleneck (~0.25 Gbps TX)
3. **Optimized V2** - Fully optimized with balanced TX/RX (~8.5 Gbps)

## Prerequisites

### System Requirements
- Linux system with LitePCIe device installed
- LitePCIe kernel driver loaded
- GCC compiler with pthread support
- Root access (for optimal performance)

### Check Device Availability
```bash
# Check if LitePCIe device exists
ls -la /dev/litepcie*

# Check kernel driver is loaded
lsmod | grep litepcie

# Check dmesg for any errors
dmesg | grep litepcie | tail -20

sudo lspci -vv -d 10ee: | grep -E "LnkCap|LnkSta"

```

## Building the Tests

### Using CMake (Recommended)

```bash
# Quick build with build script
./build.sh

# Clean build
./build.sh --clean

# Debug build
./build.sh --debug

# Manual CMake build
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

### Using Make (Alternative)

```bash
# Build both optimized versions and library
make

# Clean build
make clean
make
```

### Verify Build
```bash
# For CMake build
ls -la build/litepcie_dma_test_optimized*

# For Make build
ls -la litepcie_dma_test_optimized*
```

## Running the Tests

### Basic Commands

#### Quick Test (2 seconds)
```bash
# Run optimized V2 with default settings (CMake build)
./build/litepcie_dma_test_optimized_v2 -t 2

# Or if using Make build
./litepcie_dma_test_optimized_v2 -t 2
```

#### Performance Test (10 seconds)
```bash
# Maximum performance with zero-copy, no verification (CMake build)
./build/litepcie_dma_test_optimized_v2 -z -n -t 10

# Run benchmark target (CMake only)
cd build && make benchmark
```

#### Compare All Versions
```bash
# Original (if available)
timeout 2s litepcie_util dma_test

# V1 - Shows TX bottleneck (CMake build)
./build/litepcie_dma_test_optimized -t 2

# V2 - Balanced performance (CMake build)
./build/litepcie_dma_test_optimized_v2 -t 2
```

### Command Line Options

```bash
# CMake build
./build/litepcie_dma_test_optimized_v2 -h

# Make build
./litepcie_dma_test_optimized_v2 -h

Options:
  -d <device>    Device file (default: /dev/litepcie0)
  -p <pattern>   Pattern: 0=seq, 1=random, 2=ones, 3=zeros, 4=alt (default: 1)
  -w <width>     Data width in bits (default: 32)
  -l             Enable external loopback (default: internal)
  -z             Enable zero-copy mode
  -n             Disable data verification
  -a             Disable CPU affinity
  -i <us>        DMA poll interval in microseconds (default: 100)
  -v             Verbose output
  -t <seconds>   Test duration (0 = infinite)
```

### Test Scenarios

#### 1. Different Patterns
```bash
# Sequential pattern
./build/litepcie_dma_test_optimized_v2 -p 0 -t 5

# Random pattern (default)
./build/litepcie_dma_test_optimized_v2 -p 1 -t 5

# All ones pattern
./build/litepcie_dma_test_optimized_v2 -p 2 -t 5

# All zeros pattern
./build/litepcie_dma_test_optimized_v2 -p 3 -t 5

# Alternating pattern
./build/litepcie_dma_test_optimized_v2 -p 4 -t 5
```

#### 2. Poll Interval Testing
```bash
# Very fast polling (10 µs) - Higher CPU usage
./build/litepcie_dma_test_optimized_v2 -i 10 -t 5

# Default (100 µs) - Balanced
./build/litepcie_dma_test_optimized_v2 -i 100 -t 5

# Slower polling (1000 µs) - Lower CPU usage
./build/litepcie_dma_test_optimized_v2 -i 1000 -t 5
```

#### 3. Zero-Copy Mode
```bash
# Enable zero-copy for better memory bandwidth
./build/litepcie_dma_test_optimized_v2 -z -t 5

# Zero-copy with fast polling
./build/litepcie_dma_test_optimized_v2 -z -i 50 -t 5
```

#### 4. External Loopback
```bash
# Test with external loopback (requires hardware connection)
./build/litepcie_dma_test_optimized_v2 -l -t 5

# External loopback with data verification
./build/litepcie_dma_test_optimized_v2 -l -v -t 5
```

## Performance Optimization

### Prevent Buffer Overruns

If you see kernel messages like:
```
litepcie 0000:0d:00.0: Reading too late, 64 buffers lost
litepcie 0000:0d:00.0: Writing too late, 123 buffers lost
```

Try these solutions:

#### 1. Run with High Priority
```bash
# Run with highest scheduling priority
sudo nice -n -20 ./build/litepcie_dma_test_optimized_v2 -t 10

# Or use real-time scheduling
sudo chrt -f 99 ./build/litepcie_dma_test_optimized_v2 -t 10
```

#### 2. CPU Performance Mode
```bash
# Set CPU to performance mode (disable frequency scaling)
sudo cpupower frequency-set -g performance

# Then run the test
./build/litepcie_dma_test_optimized_v2 -t 10

# Restore to default after testing
sudo cpupower frequency-set -g ondemand
```

#### 3. CPU Isolation
```bash
# If CPUs are isolated (via isolcpus kernel parameter)
sudo taskset -c 2-3 ./build/litepcie_dma_test_optimized_v2 -t 10
```

#### 4. Optimal Performance Command
```bash
# Combine all optimizations
sudo nice -n -20 ./build/litepcie_dma_test_optimized_v2 -z -i 10 -t 10
```

### Monitor System During Test

Run these in separate terminals:

```bash
# Terminal 1: Watch for kernel messages
watch -n 1 'dmesg | tail -20'

# Terminal 2: Monitor CPU usage
htop

# Terminal 3: Monitor interrupts
watch -n 1 'cat /proc/interrupts | grep litepcie'
```

## Troubleshooting

### Common Issues and Solutions

#### 1. Permission Denied
```bash
# Run with sudo
sudo ./build/litepcie_dma_test_optimized_v2 -t 5

# Or change device permissions
sudo chmod 666 /dev/litepcie0
```

#### 2. Device Not Found
```bash
# Check if driver is loaded
sudo modprobe litepcie

# Check device exists
ls -la /dev/litepcie*
```

#### 3. Low Performance
```bash
# First, find the LitePCIe device
lspci | grep -i xilinx
# or
lspci -d 10ee:

# Note the device ID (e.g., 0d:00.0), then check PCIe link status
sudo lspci -vv -s 0d:00.0 | grep -E "LnkCap|LnkSta"

# Alternative: Check all PCIe devices for LitePCIe
for dev in $(lspci | awk '{print $1}'); do 
    info=$(sudo lspci -vv -s $dev 2>/dev/null | grep -A2 "Xilinx\|10ee")
    if [ ! -z "$info" ]; then
        echo "Device: $dev"
        sudo lspci -vv -s $dev | grep -E "LnkCap|LnkSta"
    fi
done

# Should show something like:
# LnkCap: Port #0, Speed 8GT/s, Width x1  (Gen3 x1 = ~8 Gbps)
# LnkSta: Speed 8GT/s, Width x1
```

#### 4. Compilation Errors
```bash
# Ensure all headers are present
ls -la *.h

# Check library was built
ls -la liblitepcie.a

# Rebuild from clean state
make -f Makefile.optimized_v2 clean
make -f Makefile.optimized_v2
```

## Understanding the Results

### Expected Performance

#### V1 (Original Optimization)
```
TX: ~0.25 Gbps (bottlenecked)
RX: ~4.5 Gbps
```

#### V2 (Full Optimization)
```
TX: ~8.5 Gbps
RX: ~8.5 Gbps
```

### Output Format
```
[  2.00s] TX:    8.495 Gbps (259410 buffers) | RX:    8.511 Gbps (259905 buffers) | Errors: 0 | DMA: 3808/s
```

- **Time**: Elapsed time since test started
- **TX/RX**: Throughput in Gigabits per second
- **Buffers**: Number of 8KB buffers transferred
- **Errors**: Data verification errors (if enabled)
- **DMA**: DMA process calls per second

### Performance Metrics

- **PCIe Gen3 x1**: Theoretical max ~8 Gbps (after overhead)
- **Achieved**: ~8.5 Gbps bidirectional (excellent efficiency)
- **Latency**: Sub-millisecond with proper configuration

## Advanced Usage

### Custom Test Script
```bash
#!/bin/bash
# save as run_perf_test.sh

echo "=== LitePCIe Performance Test Suite ==="

# Set performance mode
sudo cpupower frequency-set -g performance

# Run tests
echo -e "\n1. Baseline test"
./build/litepcie_dma_test_optimized_v2 -t 5

echo -e "\n2. Zero-copy mode"
./build/litepcie_dma_test_optimized_v2 -z -t 5

echo -e "\n3. Maximum performance"
sudo nice -n -20 ./build/litepcie_dma_test_optimized_v2 -z -n -i 10 -t 5

# Restore CPU governor
sudo cpupower frequency-set -g ondemand
```

Make it executable:
```bash
chmod +x run_perf_test.sh
./run_perf_test.sh
```

## Summary

The optimized V2 implementation provides:
- **34x improvement** in TX throughput over V1
- **Balanced bidirectional** performance (~8.5 Gbps each way)
- **Low latency** with configurable polling intervals
- **CPU efficient** with proper thread affinity

For best results, use:
```bash
sudo nice -n -20 ./build/litepcie_dma_test_optimized_v2 -z -i 50 -t 10
```
