# LitePCIe Round-Trip Latency Measurement

Simple tools for measuring PCIe round-trip latency with LitePCIe devices.

## Quick Start

```bash
# 1. Build everything
mkdir -p build && cd build
cmake .. && make
cd ..

# 2. Run userspace latency test (no kernel changes needed)
./build/litepcie_latency_test

# 3. (Optional) For kernel-space measurement with lower overhead:
cd kernel
make
sudo rmmod litepcie
sudo insmod litepcie.ko
cd ..
sudo ./build/litepcie_latency_kernel
```

## Test Programs

### 1. Userspace Test (`litepcie_latency_test`)
- **Latency**: ~2-10 µs (includes syscall overhead)
- **No kernel modifications needed**
- **Best for**: General latency measurements

```bash
# Basic test
./build/litepcie_latency_test

# With options
./build/litepcie_latency_test -n 50000  # More iterations
./build/litepcie_latency_test -c 2      # Pin to CPU 2
sudo ./build/litepcie_latency_test -p   # High priority
```

### 2. Kernel Test (`litepcie_latency_kernel`)
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

- Uses scratch register at 0x4 for round-trip measurement
- Userspace test includes ~1-5 µs syscall overhead
- Kernel test measures hardware latency directly
- Both tests verify data integrity during measurement