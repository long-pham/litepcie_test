/*
 * LitePCIe Round-Trip Latency Test
 * 
 * Measures PCIe round-trip latency using register read/write operations.
 * Uses the scratch register for minimal overhead measurements.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <math.h>
#include <sched.h>

#include "litepcie.h"

/* Test configuration */
#define DEFAULT_ITERATIONS 10000
#define WARMUP_ITERATIONS  1000
#define SCRATCH_REGISTER   0x4  /* CSR_CTRL_SCRATCH_ADDR */

/* Statistics structure */
typedef struct {
    double min;
    double max;
    double mean;
    double stddev;
    double p50;  /* median */
    double p90;
    double p95;
    double p99;
    double p999;
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

/* Register read via ioctl */
static uint32_t reg_read(int fd, uint32_t addr) {
    struct litepcie_ioctl_reg reg;
    reg.addr = addr;
    reg.is_write = 0;
    
    if (ioctl(fd, LITEPCIE_IOCTL_REG, &reg) < 0) {
        perror("ioctl read");
        return 0;
    }
    
    return reg.val;
}

/* Register write via ioctl */
static void reg_write(int fd, uint32_t addr, uint32_t val) {
    struct litepcie_ioctl_reg reg;
    reg.addr = addr;
    reg.val = val;
    reg.is_write = 1;
    
    if (ioctl(fd, LITEPCIE_IOCTL_REG, &reg) < 0) {
        perror("ioctl write");
    }
}

/* Measure single round-trip latency */
static uint64_t measure_latency(int fd, uint32_t test_value) {
    uint64_t start, end;
    uint32_t readback;
    
    start = get_time_ns();
    
    /* Write to scratch register */
    reg_write(fd, SCRATCH_REGISTER, test_value);
    
    /* Read back to ensure completion */
    readback = reg_read(fd, SCRATCH_REGISTER);
    
    end = get_time_ns();
    
    /* Verify data integrity */
    if (readback != test_value) {
        fprintf(stderr, "Data mismatch: wrote 0x%08x, read 0x%08x\n", 
                test_value, readback);
    }
    
    return end - start;
}

/* Compare function for qsort */
static int compare_uint64(const void *a, const void *b) {
    uint64_t ua = *(const uint64_t*)a;
    uint64_t ub = *(const uint64_t*)b;
    return (ua > ub) - (ua < ub);
}

/* Calculate statistics from latency measurements */
static void calculate_stats(uint64_t *latencies, int count, latency_stats_t *stats) {
    double sum = 0, sum_sq = 0;
    int i;
    
    /* Sort for percentiles */
    qsort(latencies, count, sizeof(uint64_t), compare_uint64);
    
    /* Calculate basic stats */
    stats->min = latencies[0] / 1000.0;  /* Convert to microseconds */
    stats->max = latencies[count-1] / 1000.0;
    
    for (i = 0; i < count; i++) {
        double val = latencies[i] / 1000.0;
        sum += val;
        sum_sq += val * val;
    }
    
    stats->mean = sum / count;
    stats->stddev = sqrt((sum_sq / count) - (stats->mean * stats->mean));
    
    /* Calculate percentiles */
    stats->p50 = latencies[count * 50 / 100] / 1000.0;
    stats->p90 = latencies[count * 90 / 100] / 1000.0;
    stats->p95 = latencies[count * 95 / 100] / 1000.0;
    stats->p99 = latencies[count * 99 / 100] / 1000.0;
    stats->p999 = latencies[count * 999 / 1000] / 1000.0;
}

/* Print statistics */
static void print_stats(latency_stats_t *stats) {
    printf("\nLatency Statistics (microseconds):\n");
    printf("  Min:    %8.3f µs\n", stats->min);
    printf("  Max:    %8.3f µs\n", stats->max);
    printf("  Mean:   %8.3f µs\n", stats->mean);
    printf("  StdDev: %8.3f µs\n", stats->stddev);
    printf("\nPercentiles:\n");
    printf("  50%%:    %8.3f µs (median)\n", stats->p50);
    printf("  90%%:    %8.3f µs\n", stats->p90);
    printf("  95%%:    %8.3f µs\n", stats->p95);
    printf("  99%%:    %8.3f µs\n", stats->p99);
    printf("  99.9%%:  %8.3f µs\n", stats->p999);
}

/* Usage */
static void usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -d <device>    Device file (default: /dev/litepcie0)\n");
    printf("  -n <count>     Number of iterations (default: %d)\n", DEFAULT_ITERATIONS);
    printf("  -w <count>     Warmup iterations (default: %d)\n", WARMUP_ITERATIONS);
    printf("  -c <cpu>       Pin to CPU core (default: no pinning)\n");
    printf("  -p             Use high priority scheduling\n");
    printf("  -v             Verbose output\n");
    printf("  -h             Show this help\n");
}

/* Main */
int main(int argc, char *argv[]) {
    const char *device = "/dev/litepcie0";
    int iterations = DEFAULT_ITERATIONS;
    int warmup = WARMUP_ITERATIONS;
    int cpu_core = -1;
    int high_priority = 0;
    int verbose = 0;
    int opt, fd;
    uint64_t *latencies;
    latency_stats_t stats;
    int i;
    
    /* Parse options */
    while ((opt = getopt(argc, argv, "d:n:w:c:pvh")) != -1) {
        switch (opt) {
        case 'd':
            device = optarg;
            break;
        case 'n':
            iterations = atoi(optarg);
            if (iterations < 1) {
                fprintf(stderr, "Invalid iteration count: %d\n", iterations);
                return 1;
            }
            break;
        case 'w':
            warmup = atoi(optarg);
            break;
        case 'c':
            cpu_core = atoi(optarg);
            break;
        case 'p':
            high_priority = 1;
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
            fprintf(stderr, "Warning: Failed to set CPU affinity\n");
        } else {
            printf("Process pinned to CPU core %d\n", cpu_core);
        }
    }
    
    /* Set high priority if requested */
    if (high_priority) {
        struct sched_param param;
        param.sched_priority = sched_get_priority_max(SCHED_FIFO);
        if (sched_setscheduler(0, SCHED_FIFO, &param) < 0) {
            perror("sched_setscheduler");
            fprintf(stderr, "Warning: Failed to set real-time priority (try running as root)\n");
        } else {
            printf("Running with real-time priority\n");
        }
    }
    
    /* Open device */
    fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("open");
        fprintf(stderr, "Failed to open %s\n", device);
        return 1;
    }
    
    /* Allocate measurement buffer */
    latencies = malloc(iterations * sizeof(uint64_t));
    if (!latencies) {
        fprintf(stderr, "Failed to allocate memory\n");
        close(fd);
        return 1;
    }
    
    printf("LitePCIe Round-Trip Latency Test\n");
    printf("Device: %s\n", device);
    printf("Iterations: %d (after %d warmup)\n", iterations, warmup);
    printf("Measuring latency using scratch register at 0x%08x\n\n", SCRATCH_REGISTER);
    
    /* Warmup */
    if (warmup > 0) {
        printf("Warming up...");
        fflush(stdout);
        for (i = 0; i < warmup && keep_running; i++) {
            measure_latency(fd, 0x12345678 + i);
        }
        printf(" done\n");
    }
    
    /* Main measurement loop */
    printf("Measuring latency...");
    fflush(stdout);
    
    for (i = 0; i < iterations && keep_running; i++) {
        uint32_t test_value = 0xDEADBEEF ^ i;  /* Varying test pattern */
        latencies[i] = measure_latency(fd, test_value);
        
        if (verbose && (i % 1000 == 0)) {
            printf("\r  Progress: %d/%d (%.1f%%)", 
                   i, iterations, 100.0 * i / iterations);
            fflush(stdout);
        }
    }
    
    if (verbose) {
        printf("\r                                          \r");
    }
    printf(" done\n");
    
    /* Calculate and print statistics */
    if (i > 0) {
        calculate_stats(latencies, i, &stats);
        print_stats(&stats);
        
        /* Additional analysis */
        printf("\nAnalysis:\n");
        printf("  Total measurements: %d\n", i);
        printf("  Approximate overhead: ~%.1f µs per syscall\n", stats.min / 2);
        printf("  Estimated PCIe RTT: ~%.1f µs\n", stats.min - (stats.min / 2));
    }
    
    /* Cleanup */
    free(latencies);
    close(fd);
    
    return 0;
}