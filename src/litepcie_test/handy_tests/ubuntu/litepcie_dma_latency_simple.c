/*
 * LitePCIe DMA Latency Test - Simple Version
 * 
 * Uses liblitepcie API to measure DMA round-trip latency in loopback mode
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <math.h>
#include <sched.h>

#include "litepcie.h"
#include "liblitepcie.h"
#include "litepcie_dma.h"

/* Test configuration */
#define DEFAULT_ITERATIONS 1000
#define WARMUP_ITERATIONS  100
#define MIN_BUFFER_SIZE    64
#define TEST_PATTERN       0xCAFEBABE

/* Statistics structure */
typedef struct {
    double min;
    double max;
    double mean;
    double stddev;
    double p50;
    double p90;
    double p95;
    double p99;
} latency_stats_t;

/* Global state */
static volatile int keep_running = 1;

/* Get current time in nanoseconds */
static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Signal handler */
static void signal_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

/* Measure single DMA round-trip latency */
static uint64_t measure_dma_latency(struct litepcie_dma_ctrl *dma, int size, int verbose) {
    uint64_t start, end;
    char *wr_buf, *rd_buf;
    int timeout;
    int processed;
    
    /* Wait for write buffer */
    timeout = 0;
    while ((wr_buf = litepcie_dma_next_write_buffer(dma)) == NULL && timeout < 100) {
        litepcie_dma_process(dma);
        timeout++;
        usleep(10);
    }
    if (!wr_buf) {
        if (verbose) fprintf(stderr, "No write buffer available\n");
        return UINT64_MAX;
    }
    
    /* Fill buffer with test pattern */
    uint32_t *data = (uint32_t*)wr_buf;
    for (int i = 0; i < size/sizeof(uint32_t); i++) {
        data[i] = TEST_PATTERN + i;
    }
    
    /* Clear read buffers */
    while ((rd_buf = litepcie_dma_next_read_buffer(dma)) != NULL) {
        /* Consume any pending reads */
    }
    
    /* Start timing */
    start = get_time_ns();
    
    /* Process DMA to send write buffer */
    processed = 0;
    while (processed < 10) {
        litepcie_dma_process(dma);
        processed++;
        
        /* Check if we got data back (loopback) */
        rd_buf = litepcie_dma_next_read_buffer(dma);
        if (rd_buf) {
            end = get_time_ns();
            
            /* Verify data */
            uint32_t *rx_data = (uint32_t*)rd_buf;
            for (int i = 0; i < size/sizeof(uint32_t); i++) {
                if (rx_data[i] != TEST_PATTERN + i) {
                    if (verbose) {
                        fprintf(stderr, "Data mismatch at %d: expected 0x%08x, got 0x%08x\n",
                                i, TEST_PATTERN + i, rx_data[i]);
                    }
                    return UINT64_MAX;
                }
            }
            
            return end - start;
        }
        
        usleep(1);
    }
    
    if (verbose) fprintf(stderr, "Timeout waiting for loopback data\n");
    return UINT64_MAX;
}

/* Compare function for qsort */
static int compare_uint64(const void *a, const void *b) {
    uint64_t ua = *(const uint64_t*)a;
    uint64_t ub = *(const uint64_t*)b;
    return (ua > ub) - (ua < ub);
}

/* Calculate statistics */
static void calculate_stats(uint64_t *latencies, int count, latency_stats_t *stats) {
    double sum = 0, sum_sq = 0;
    int valid_count = 0;
    
    /* Filter out timeouts */
    for (int i = 0; i < count; i++) {
        if (latencies[i] != UINT64_MAX) {
            valid_count++;
        }
    }
    
    if (valid_count == 0) {
        memset(stats, 0, sizeof(*stats));
        return;
    }
    
    /* Sort for percentiles */
    qsort(latencies, count, sizeof(uint64_t), compare_uint64);
    
    /* Calculate stats */
    stats->min = latencies[0] / 1000.0;
    stats->max = latencies[valid_count-1] / 1000.0;
    
    for (int i = 0; i < valid_count; i++) {
        double val = latencies[i] / 1000.0;
        sum += val;
        sum_sq += val * val;
    }
    
    stats->mean = sum / valid_count;
    stats->stddev = sqrt((sum_sq / valid_count) - (stats->mean * stats->mean));
    
    /* Percentiles */
    stats->p50 = latencies[valid_count * 50 / 100] / 1000.0;
    stats->p90 = latencies[valid_count * 90 / 100] / 1000.0;
    stats->p95 = latencies[valid_count * 95 / 100] / 1000.0;
    stats->p99 = latencies[valid_count * 99 / 100] / 1000.0;
}

/* Print statistics */
static void print_stats(latency_stats_t *stats) {
    printf("\nDMA Latency Statistics (microseconds):\n");
    printf("  Min:    %8.3f µs\n", stats->min);
    printf("  Max:    %8.3f µs\n", stats->max);
    printf("  Mean:   %8.3f µs\n", stats->mean);
    printf("  StdDev: %8.3f µs\n", stats->stddev);
    printf("\nPercentiles:\n");
    printf("  50%%:    %8.3f µs (median)\n", stats->p50);
    printf("  90%%:    %8.3f µs\n", stats->p90);
    printf("  95%%:    %8.3f µs\n", stats->p95);
    printf("  99%%:    %8.3f µs\n", stats->p99);
}

/* Print histogram */
static void print_histogram(uint64_t *latencies, int count) {
    const int num_bins = 20;
    const int bar_width = 50;
    int bins[num_bins];
    double bin_width;
    double min_us, max_us;
    int max_count = 0;
    int valid_count = 0;
    
    /* Initialize bins */
    memset(bins, 0, sizeof(bins));
    
    /* Find min/max of valid measurements */
    min_us = 1e9;
    max_us = 0;
    for (int i = 0; i < count; i++) {
        if (latencies[i] != UINT64_MAX) {
            double us = latencies[i] / 1000.0;
            if (us < min_us) min_us = us;
            if (us > max_us) max_us = us;
            valid_count++;
        }
    }
    
    if (valid_count == 0) return;
    
    /* Calculate bin width */
    bin_width = (max_us - min_us) / num_bins;
    if (bin_width == 0) bin_width = 1;
    
    /* Fill bins */
    for (int i = 0; i < count; i++) {
        if (latencies[i] != UINT64_MAX) {
            double us = latencies[i] / 1000.0;
            int bin = (int)((us - min_us) / bin_width);
            if (bin >= num_bins) bin = num_bins - 1;
            if (bin < 0) bin = 0;
            bins[bin]++;
            if (bins[bin] > max_count) max_count = bins[bin];
        }
    }
    
    /* Print histogram */
    printf("\nLatency Distribution Histogram:\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    for (int i = 0; i < num_bins; i++) {
        double bin_start = min_us + i * bin_width;
        double bin_end = bin_start + bin_width;
        int bar_len = max_count > 0 ? (bins[i] * bar_width) / max_count : 0;
        
        printf("%6.1f-%6.1f µs [%4d] ", bin_start, bin_end, bins[i]);
        
        /* Print bar */
        for (int j = 0; j < bar_len; j++) {
            printf("█");
        }
        
        /* Print percentage */
        double percent = valid_count > 0 ? (100.0 * bins[i]) / valid_count : 0;
        printf(" %.1f%%\n", percent);
    }
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

/* Usage */
static void usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -d <device>    Device file (default: /dev/litepcie0)\n");
    printf("  -n <count>     Number of iterations (default: %d)\n", DEFAULT_ITERATIONS);
    printf("  -w <count>     Warmup iterations (default: %d)\n", WARMUP_ITERATIONS);
    printf("  -s <size>      Test size in bytes (default: %d)\n", MIN_BUFFER_SIZE);
    printf("  -c <cpu>       Pin to CPU core\n");
    printf("  -z             Use zero-copy mode\n");
    printf("  -v             Verbose output\n");
    printf("  -h             Show this help\n");
}

/* Main */
int main(int argc, char *argv[]) {
    const char *device = "/dev/litepcie0";
    int iterations = DEFAULT_ITERATIONS;
    int warmup = WARMUP_ITERATIONS;
    int test_size = MIN_BUFFER_SIZE;
    int cpu_core = -1;
    int verbose = 0;
    uint8_t zero_copy = 0;
    int opt;
    uint64_t *latencies;
    latency_stats_t stats;
    struct litepcie_dma_ctrl dma;
    
    /* Parse options */
    while ((opt = getopt(argc, argv, "d:n:w:s:c:zvh")) != -1) {
        switch (opt) {
        case 'd':
            device = optarg;
            break;
        case 'n':
            iterations = atoi(optarg);
            break;
        case 'w':
            warmup = atoi(optarg);
            break;
        case 's':
            test_size = atoi(optarg);
            if (test_size < MIN_BUFFER_SIZE || test_size > DMA_BUFFER_SIZE) {
                fprintf(stderr, "Test size must be between %d and %d bytes\n",
                        MIN_BUFFER_SIZE, DMA_BUFFER_SIZE);
                return 1;
            }
            break;
        case 'c':
            cpu_core = atoi(optarg);
            break;
        case 'z':
            zero_copy = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }
    
    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    
    /* Set CPU affinity if requested */
    if (cpu_core >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_core, &cpuset);
        if (sched_setaffinity(0, sizeof(cpuset), &cpuset) < 0) {
            perror("sched_setaffinity");
        } else if (verbose) {
            printf("Pinned to CPU core %d\n", cpu_core);
        }
    }
    
    /* Initialize DMA */
    memset(&dma, 0, sizeof(dma));
    dma.loopback = 1;  /* Enable loopback for round-trip measurement */
    dma.use_reader = 1;
    dma.use_writer = 1;
    
    if (litepcie_dma_init(&dma, device, zero_copy)) {
        fprintf(stderr, "Failed to initialize DMA\n");
        return 1;
    }
    
    /* Enable DMA channels */
    dma.reader_enable = 1;
    dma.writer_enable = 1;
    
    /* Allocate results buffer */
    latencies = malloc(iterations * sizeof(uint64_t));
    if (!latencies) {
        fprintf(stderr, "Failed to allocate memory\n");
        litepcie_dma_cleanup(&dma);
        return 1;
    }
    
    printf("LitePCIe DMA Latency Test (Simple)\n");
    printf("Device: %s\n", device);
    printf("Mode: %s loopback\n", dma.loopback ? "Internal" : "External");
    printf("Test size: %d bytes\n", test_size);
    printf("Zero-copy: %s\n", zero_copy ? "Yes" : "No");
    printf("Iterations: %d (after %d warmup)\n\n", iterations, warmup);
    
    /* Warmup */
    if (warmup > 0) {
        printf("Warming up...");
        fflush(stdout);
        for (int i = 0; i < warmup && keep_running; i++) {
            measure_dma_latency(&dma, test_size, verbose);
        }
        printf(" done\n");
    }
    
    /* Main measurement loop */
    printf("Measuring DMA latency...");
    fflush(stdout);
    
    int valid = 0;
    for (int i = 0; i < iterations && keep_running; i++) {
        latencies[i] = measure_dma_latency(&dma, test_size, verbose);
        if (latencies[i] != UINT64_MAX) {
            valid++;
        }
        
        if (verbose && (i % 100 == 0)) {
            printf("\r  Progress: %d/%d (%.1f%%), valid: %d", 
                   i+1, iterations, 100.0 * (i+1) / iterations, valid);
            fflush(stdout);
        }
    }
    
    if (verbose) {
        printf("\r                                                    \r");
    }
    printf(" done\n");
    
    /* Calculate and print statistics */
    if (valid > 0) {
        calculate_stats(latencies, iterations, &stats);
        print_stats(&stats);
        
        /* Print histogram */
        print_histogram(latencies, iterations);
        
        printf("\nAnalysis:\n");
        printf("  Valid measurements: %d/%d (%.1f%%)\n", 
               valid, iterations, 100.0 * valid / iterations);
        printf("  Buffer utilization: %.1f%% of %d bytes\n",
               100.0 * test_size / DMA_BUFFER_SIZE, DMA_BUFFER_SIZE);
        
        if (valid < iterations * 0.9) {
            printf("\nWarning: High failure rate. Check:\n");
            printf("  - DMA loopback is functioning\n");
            printf("  - Buffer sizes are appropriate\n");
            printf("  - No other processes using the device\n");
        }
    } else {
        printf("\nError: All measurements failed!\n");
        printf("Check that DMA loopback is working properly.\n");
    }
    
    /* Cleanup */
    free(latencies);
    litepcie_dma_cleanup(&dma);
    
    return 0;
}