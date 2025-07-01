# LitePCIe Round-Trip Latency Measurement

Comprehensive tools for measuring PCIe round-trip latency and DMA performance with LitePCIe devices.

## Quick Start

```bash
# 1. Build everything
mkdir -p build && cd build
cmake .. && make
cd ..

# 2. Run userspace latency test (no kernel changes needed)
./build/litepcie_latency_test

# 3. Run DMA latency tests
./build/litepcie_dma_latency_test       # Comprehensive test
./build/litepcie_dma_latency_simple    # Simple measurement

# 4. (Optional) For kernel-space measurement with lower overhead:
cd kernel
make
sudo rmmod litepcie
sudo insmod litepcie.ko
cd ..
sudo ./build/litepcie_latency_kernel
```

## Test Programs

### 1. Register Access Latency Test (`litepcie_latency_test`)
- **Latency**: ~2-10 µs (includes syscall overhead)
- **No kernel modifications needed**
- **Best for**: General register access latency measurements

```bash
# Basic test
./build/litepcie_latency_test

# With options
./build/litepcie_latency_test -n 50000  # More iterations
./build/litepcie_latency_test -c 2      # Pin to CPU 2
sudo ./build/litepcie_latency_test -p   # High priority
```

### 2. DMA Latency Test (`litepcie_dma_latency_test`)
- **Features**: Comprehensive DMA performance testing with latency measurements
- **Measures**: DMA setup time, transfer time, completion polling
- **Best for**: Full DMA performance characterization

```bash
# Basic test
./build/litepcie_dma_latency_test

# With options
./build/litepcie_dma_latency_test -n 1000    # Number of transfers
./build/litepcie_dma_latency_test -s 8192    # Buffer size (bytes)
./build/litepcie_dma_latency_test -b 32      # Number of buffers
./build/litepcie_dma_latency_test -t 10      # Test duration (seconds)
./build/litepcie_dma_latency_test -v         # Verbose output
```

### 3. Simple DMA Latency Test (`litepcie_dma_latency_simple`)
- **Features**: Basic DMA round-trip latency measurement
- **Minimal overhead**: Direct measurement without complex statistics
- **Best for**: Quick DMA latency checks

```bash
# Basic test
./build/litepcie_dma_latency_simple

# With options
./build/litepcie_dma_latency_simple -n 1000   # Number of iterations
./build/litepcie_dma_latency_simple -s 4096   # Buffer size
```

### 4. Kernel Test (`litepcie_latency_kernel`)
- **Latency**: ~0.3-3 µs (hardware only)
- **Requires modified kernel module**
- **Best for**: Precise hardware characterization

```bash
# Build and load modified kernel module
cd kernel && make && cd ..
sudo rmmod litepcie
sudo insmod kernel/litepcie.ko

# Run kernel test
sudo ./build/litepcie_latency_kernel
```

## Example Output

### Register Access Latency
```
LitePCIe Round-Trip Latency Test
Device: /dev/litepcie0
Iterations: 10000 (after 1000 warmup)

Latency Statistics (microseconds):
  Min:       2.345 µs
  Max:      15.678 µs
  Mean:      2.567 µs
  StdDev:    0.234 µs

Percentiles:
  50%:       2.456 µs (median)
  90%:       2.789 µs
  95%:       3.012 µs
  99%:       4.567 µs
  99.9%:     8.901 µs
```

### DMA Latency Test Output
```
LitePCIe DMA Latency Test
Device: /dev/litepcie0
DMA Buffers: 32 x 8192 bytes

DMA Operation Latencies (microseconds):
  Setup:     12.34 µs (avg)
  Transfer:  45.67 µs (avg)
  Complete:  58.90 µs (avg)

Throughput: 8.45 Gbps
Total transfers: 10000
```

## Performance Tips

1. **Reduce variance**: `sudo cpupower frequency-set -g performance`
2. **Pin to CPU**: Use `-c <cpu>` option
3. **High priority**: Run with `sudo` and `-p` flag
4. **Check results**: Lower is better, watch for consistency

## Troubleshooting

**Test crashes or permission denied:**
```bash
# Check device exists
ls -la /dev/litepcie*

# Check kernel module
lsmod | grep litepcie

# View errors
sudo dmesg | tail -20
```

**High latency variance:**
- Disable CPU throttling
- Stop unnecessary services
- Use CPU pinning (`-c` option)

## Technical Details

### Register Access Tests
- Uses scratch register at 0x4 for round-trip measurement
- Userspace test includes ~1-5 µs syscall overhead
- Kernel test measures hardware latency directly
- All tests verify data integrity during measurement

### DMA Latency Tests
- **litepcie_dma_latency_test**: Full-featured test with detailed statistics
  - Measures DMA setup, transfer, and completion times separately
  - Supports configurable buffer sizes and counts
  - Provides throughput measurements alongside latency
  - Includes percentile statistics for latency distribution

- **litepcie_dma_latency_simple**: Lightweight latency measurement
  - Minimal overhead for accurate measurements
  - Single buffer round-trip timing
  - Ideal for quick latency checks
  - No complex statistics to reduce measurement overhead