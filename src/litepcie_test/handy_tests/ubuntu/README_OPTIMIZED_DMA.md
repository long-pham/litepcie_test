# LitePCIe Optimized DMA Test

This is an optimized version of the LitePCIe DMA test utility that provides significant performance improvements over the original implementation.

## Key Optimizations

1. **Asynchronous Processing**: Separate reader/writer threads for concurrent operation
2. **Memory Optimizations**: 
   - Cache-line aligned buffers
   - Prefetch hints for better cache utilization
   - Batch processing to reduce overhead
3. **CPU Affinity**: Threads pinned to specific cores to reduce context switching
4. **Zero-Copy Support**: Optional mmap-based buffer management
5. **Configurable Options**: Fine-tune performance based on your hardware

## Project Structure

### DMA Test Programs
- `litepcie_dma_test_optimized.c` - Optimized version 1
- `litepcie_dma_test_optimized_v2.c` - Fully optimized version 2

### User Utilities (in `user/` directory)
- `litepcie_util.c` - General utility for LitePCIe operations (info, dma_test, scratch test)
- `litepcie_test.c` - Comprehensive test suite
- `litepcie_latency_test.c` - Detailed latency measurements
- `litepcie_latency_test_simple.c` - Simple latency test
- `litepcie_latency_test_final.c` - Final optimized latency test

### Library and Modules
- `user/liblitepcie/` - Local LitePCIe library sources
- `kernel/` - LitePCIe kernel module sources (optional)

### Build Files
- `CMakeLists.txt` - CMake build configuration
- `Makefile` - Traditional make build
- `build.sh` - Convenient build script

## Building

### Using CMake (Recommended)

```bash
# Quick build
./build.sh

# Debug build
./build.sh --debug

# Clean build
./build.sh --clean

# Build with kernel modules (Linux only)
./build.sh --kernel

# Manual CMake build
mkdir -p build
cd build
cmake ..
make -j$(nproc)

# Build kernel modules after CMake configure
cd build
make kernel_modules
sudo make install_kernel_modules
sudo make load_kernel_modules
```

### Using Make (Alternative)

```bash
# Build both optimized versions
make

# Clean build artifacts
make clean
```

## Usage

### DMA Test Programs

```bash
./litepcie_dma_test_optimized [options]

Options:
  -d <device>    Device file (default: /dev/litepcie0)
  -p <pattern>   Pattern: 0=seq, 1=random, 2=ones, 3=zeros, 4=alt (default: 1)
  -w <width>     Data width in bits (default: 32)
  -l             Enable external loopback (default: internal)
  -z             Enable zero-copy mode
  -n             Disable data verification (for maximum performance)
  -a             Disable CPU affinity
  -b             Disable batch processing
  -v             Verbose output
  -t <seconds>   Test duration (0 = infinite)
  -h             Show help
```

### User Utilities

```bash
# General utility - device info, DMA test, scratch test
./build/litepcie_util info
./build/litepcie_util dma_test
./build/litepcie_util scratch_test

# Comprehensive test suite
./build/litepcie_test

# Latency tests
./build/litepcie_latency_test          # Detailed measurements
./build/litepcie_latency_test_simple   # Simple test
./build/litepcie_latency_test_final    # Optimized version
```

## Running Tests

### Quick Test
```bash
# Run a 10-second test with default settings
./build/litepcie_dma_test_optimized -t 10

# Or if using Make build
./litepcie_dma_test_optimized -t 10
```

### Performance Test
```bash
# Maximum performance mode (no verification, zero-copy)
./build/litepcie_dma_test_optimized -z -n -t 10

# Run benchmark for both versions (CMake build)
cd build && make benchmark
```

### Automated Test Suite
```bash
# Run all test variations
./test_optimized_dma.sh
```

### Performance Comparison
```bash
# Compare with original implementation
python3 compare_performance.py
```

## Performance Improvements

Expected improvements over the original implementation:

- **Throughput**: 2-5x improvement depending on configuration
- **CPU Usage**: 20-40% reduction due to batching and prefetching
- **Latency**: Lower and more consistent latency with lock-free design

### Optimization Tips

1. **For Maximum Throughput**:
   ```bash
   ./build/litepcie_dma_test_optimized -z -n -t 10
   ```
   - Use zero-copy mode (-z)
   - Disable verification (-n)
   - Keep CPU affinity enabled (default)

2. **For Data Integrity Testing**:
   ```bash
   ./build/litepcie_dma_test_optimized -p 1 -v -t 10
   ```
   - Use random pattern (-p 1)
   - Enable verbose mode (-v)
   - Keep verification enabled (default)

3. **For Low CPU Usage**:
   ```bash
   ./build/litepcie_dma_test_optimized -b -a -t 10
   ```
   - Disable batch processing (-b)
   - Disable CPU affinity (-a)

## Architecture

The optimized implementation uses:

1. **Producer-Consumer Pattern**: Separate threads for TX and RX
2. **Lock-Free Design**: Minimal synchronization overhead
3. **Memory Prefetching**: Explicit cache hints for better performance
4. **Batch Processing**: Process multiple buffers to amortize overhead

## Troubleshooting

### Permission Denied
```bash
# Run with sudo if needed
sudo ./build/litepcie_dma_test_optimized
```

### Device Not Found
```bash
# Check if device exists
ls -la /dev/litepcie*

# Load driver if needed
sudo modprobe litepcie
```

### Poor Performance
1. Check system load: `top` or `htop`
2. Verify CPU frequency scaling: `cpupower frequency-info`
3. Check PCIe link status: `lspci -vv | grep -A20 "LitePCIe"`

## Comparison with Original

| Feature | Original | Optimized |
|---------|----------|-----------|
| Threading | Single | Multi-threaded |
| Buffer Size | Fixed 8KB | Configurable |
| CPU Affinity | No | Yes |
| Prefetching | No | Yes |
| Batch Processing | No | Yes |
| Zero-Copy | Limited | Full support |

## Development

To enable debug output:
```bash
# Using CMake
./build.sh --clean --debug
./build/litepcie_dma_test_optimized -v

# Using Make
make clean
make DEBUG=1
./litepcie_dma_test_optimized -v
```

## License

Same as LitePCIe project (BSD-2-Clause)