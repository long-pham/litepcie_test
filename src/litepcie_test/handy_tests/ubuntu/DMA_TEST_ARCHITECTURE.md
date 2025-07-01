# LitePCIe DMA Test Architecture Guide

This document explains the architecture and operation of the LitePCIe DMA tests, focusing on:
1. `litepcie_dma_latency_test.c` - Measures DMA round-trip latency
2. `litepcie_dma_test_optimized_v2.c` - Maximizes DMA throughput performance

## Table of Contents
- [Overview](#overview)
- [DMA Latency Test](#dma-latency-test)
- [DMA Test Optimized V2](#dma-test-optimized-v2)
- [Key Differences](#key-differences)
- [Performance Optimization Techniques](#performance-optimization-techniques)

## Overview

Both tests use a multi-threaded architecture to separate concerns and maximize performance:
- **DMA Processing Thread**: Handles `litepcie_dma_process()` calls
- **Worker Threads**: Perform data generation/verification
- **Monitor Thread**: Updates statistics in real-time

### Common Architecture Pattern

```
┌─────────────────────┐     ┌─────────────────────┐     ┌─────────────────────┐
│   DMA Thread        │     │   Worker Thread(s)  │     │   Monitor Thread    │
│                     │     │                     │     │                     │
│ litepcie_dma_process│◄────┤ Generate/Verify Data├────►│ Update Statistics   │
│                     │     │                     │     │                     │
└─────────────────────┘     └─────────────────────┘     └─────────────────────┘
         ▲                             ▲                           ▲
         │                             │                           │
         └─────────────────────────────┴───────────────────────────┘
                              Shared DMA Control
```

## DMA Latency Test

### Purpose
Measures the round-trip latency of DMA transfers to MAIN_RAM, providing detailed latency distribution analysis.

### Architecture

```c
// Key components
1. Latency Measurement Thread (latency_thread_func)
   - Generates test patterns
   - Initiates DMA transfers
   - Measures round-trip time
   - Verifies data integrity

2. DMA Processing Thread (dma_thread_func)
   - Calls litepcie_dma_process() continuously
   - Configurable polling interval (default: 10µs)

3. Monitor Thread (monitor_thread_func)
   - Updates statistics every second
   - Only active in continuous mode
```

### Measurement Process

```
1. Generate Pattern → 2. Get Write Buffer → 3. Copy Data → 4. Start Timer
                                                               │
                                                               ▼
8. Calculate Latency ← 7. End Timer ← 6. Copy Data ← 5. Get Read Buffer
```

### Key Features

1. **Small Transfer Sizes**: Default 64 bytes for accurate latency measurement
2. **Histogram Support**: 1µs buckets up to 1ms for distribution analysis
3. **Percentile Calculations**: 50th, 90th, 95th, 99th, 99.9th percentiles
4. **Multiple Test Patterns**:
   - Sequential: Incremental values with iteration counter
   - Random: Pseudo-random data
   - Fixed: 0xDEADBEEF pattern
   - Walking: Rotating bit patterns

### Statistics Collected

```c
typedef struct {
    uint64_t count;        // Total measurements
    uint64_t errors;       // Verification errors
    double min_us;         // Minimum latency
    double max_us;         // Maximum latency
    double sum_us;         // Sum for mean calculation
    double sum_sq_us;      // Sum of squares for stddev
    uint64_t *recent_samples;  // For percentile calculation
} latency_stats_t;
```

### Usage Examples

```bash
# Basic latency test (10k iterations)
./litepcie_dma_latency_test

# Continuous monitoring mode
./litepcie_dma_latency_test -C -s 256

# High-precision test with CPU pinning
./litepcie_dma_latency_test -c 2 -n 100000 -w 10000

# Test with different patterns
./litepcie_dma_latency_test -p 0  # Sequential
./litepcie_dma_latency_test -p 1  # Random
./litepcie_dma_latency_test -p 2  # Fixed
./litepcie_dma_latency_test -p 3  # Walking ones
```

## DMA Test Optimized V2

### Purpose
Maximizes DMA throughput by optimizing buffer management and minimizing latency between operations.

### Architecture

```c
// Key components
1. Writer Thread (writer_thread_func)
   - Continuously generates data
   - Fills available DMA write buffers
   - Uses prefetching for performance

2. Reader Thread (reader_thread_func)
   - Processes received data
   - Verifies patterns if enabled
   - Tracks receive statistics

3. DMA Processing Thread (dma_thread_func)
   - High-frequency polling (default: 100µs)
   - Processes DMA descriptors
```

### Optimization Techniques

1. **Reduced Polling Interval**: 100µs vs 100ms in V1
2. **Prefetching**: Memory prefetch hints for better cache utilization
3. **Batch Processing**: Process multiple buffers when available
4. **CPU Affinity**: Pin threads to specific cores
5. **Zero-Copy Option**: Direct buffer access without copying

### Buffer Management

```
Writer Thread                    DMA Engine                    Reader Thread
     │                               │                              │
     ▼                               ▼                              ▼
┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐
│ Buffer  │───►│ Buffer  │───►│ Buffer  │───►│ Buffer  │───►│ Buffer  │
│ (Full)  │    │ (DMA)   │    │ (DMA)   │    │ (Ready) │    │ (Empty) │
└─────────┘    └─────────┘    └─────────┘    └─────────┘    └─────────┘
```

### Performance Features

1. **Buffer Configuration**:
   ```c
   #define DMA_BUFFER_SIZE    8192   // 8KB buffers
   #define DMA_BUFFER_COUNT   256    // Total buffers
   #define BATCH_SIZE         16     // Process in batches
   ```

2. **Thread Coordination**:
   - Separate mutexes for stats and DMA operations
   - Minimal lock contention
   - Yield CPU when buffers unavailable

3. **Memory Optimization**:
   ```c
   // Prefetch next cache lines
   prefetch_read(src + i + 64);
   prefetch_write(dst + i + 64);
   
   // Copy in chunks
   memcpy(dst + i, src + i, 16 * sizeof(uint32_t));
   ```

### Usage Examples

```bash
# Basic throughput test
./litepcie_dma_test_optimized_v2

# With external loopback
./litepcie_dma_test_optimized_v2 -l

# Zero-copy mode
./litepcie_dma_test_optimized_v2 -z

# Custom polling interval
./litepcie_dma_test_optimized_v2 -i 50  # 50µs polling

# Disable CPU affinity
./litepcie_dma_test_optimized_v2 -a

# Run for specific duration
./litepcie_dma_test_optimized_v2 -t 60  # 60 seconds
```

## Key Differences

| Feature | DMA Latency Test | DMA Test Optimized V2 |
|---------|------------------|----------------------|
| **Primary Goal** | Measure latency | Maximize throughput |
| **Transfer Size** | Small (64B default) | Large (8KB default) |
| **Buffer Count** | 2 (ping-pong) | 256 (queue) |
| **Measurement** | Per-operation timing | Aggregate bandwidth |
| **Patterns** | 4 types | 5 types |
| **Statistics** | Latency distribution | Throughput rates |
| **CPU Affinity** | Optional | Enabled by default |
| **Verification** | Default enabled | Default disabled (loopback) |

## Performance Optimization Techniques

### 1. Thread Isolation
- Separate threads for different tasks
- Minimal inter-thread communication
- Lock-free where possible

### 2. Memory Access Patterns
- Cache-line aligned buffers
- Prefetching for sequential access
- Minimize false sharing

### 3. DMA Engine Utilization
- Keep DMA queues full
- Minimize descriptor setup overhead
- Batch operations when possible

### 4. CPU Optimization
- Pin threads to specific cores
- Avoid context switches
- Use appropriate polling intervals

### 5. Measurement Accuracy
- High-resolution timers (nanosecond precision)
- Warmup iterations to stabilize caches
- Statistical analysis for reliability

## Troubleshooting

### Common Issues

1. **High Latency Variance**
   - Check CPU frequency scaling
   - Disable power management
   - Use CPU affinity

2. **Low Throughput**
   - Increase buffer count
   - Reduce polling interval
   - Check PCIe link speed

3. **Verification Errors**
   - Ensure proper DMA configuration
   - Check memory barriers
   - Verify FPGA design

### Performance Tuning

```bash
# Disable CPU frequency scaling
sudo cpupower frequency-set -g performance

# Set interrupt affinity
sudo sh -c "echo 2 > /proc/irq/[IRQ_NUMBER]/smp_affinity"

# Increase PCIe payload size
sudo setpci -s [BUS:DEV.FN] 68.w=5936
```

## Conclusion

Both tests serve complementary purposes:
- Use the latency test to characterize system responsiveness
- Use the optimized test to measure maximum achievable bandwidth

The multi-threaded architecture and optimization techniques can be adapted for other high-performance DMA applications.