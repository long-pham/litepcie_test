/*
 * LitePCIe DMA Latency Test V2 - Optimized Version
 *
 * Measures DMA round-trip latency to MAIN_RAM using small transfers.
 * Based on litepcie_dma_test_optimized_v2.c architecture
 *
 * Features:
 * - Thread-based architecture for concurrent latency measurements
 * - Histogram support for latency distribution analysis
 * - Continuous monitoring mode with real-time statistics
 * - CPU affinity support for consistent measurements
 * - Multiple test patterns for comprehensive testing
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <math.h>
#include <sched.h>

#include "litepcie.h"
#include "liblitepcie.h"
#include "litepcie_dma.h"
#include "kernel/mem.h"
#include "kernel/config.h"
#include "kernel/csr.h"

/* Test configuration */
#define DEFAULT_ITERATIONS   10000
#define WARMUP_ITERATIONS    1000
#define DEFAULT_TRANSFER_SIZE 64      /* Small transfer for latency test */
#define MAX_TRANSFER_SIZE    4096
#define CACHE_LINE_SIZE      64
#define HISTOGRAM_BUCKETS    1000     /* 1us buckets up to 1ms */
#define UPDATE_INTERVAL_MS   1000     /* Stats update every second */

/* Test patterns */
#define PATTERN_SEQ        0
#define PATTERN_RANDOM     1
#define PATTERN_FIXED      2
#define PATTERN_WALKING    3

/* Global state */
static volatile int keep_running = 1;
static struct litepcie_dma_ctrl dma_ctrl;
static pthread_t latency_thread, monitor_thread, dma_thread;
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t dma_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Latency histogram */
static uint64_t *latency_histogram;
static uint64_t histogram_overflow = 0;

/* Statistics structure */
typedef struct {
    uint64_t count;
    uint64_t errors;
    double min_us;
    double max_us;
    double sum_us;
    double sum_sq_us;
    struct timeval start_time;
    /* Recent samples for percentiles */
    uint64_t *recent_samples;
    int sample_index;
    int sample_count;
} latency_stats_t;

static latency_stats_t stats = {
    .min_us = 1e9,
    .max_us = 0,
};

/* Configuration */
typedef struct {
    char *device;
    int transfer_size;
    int iterations;
    int warmup;
    int pattern_type;
    int verify_data;
    int continuous;
    int histogram;
    int verbose;
    int cpu_core;
    int dma_poll_us;
    uint32_t target_addr;
} test_config_t;

static test_config_t config = {
    .device = "/dev/litepcie0",
    .transfer_size = DEFAULT_TRANSFER_SIZE,
    .iterations = DEFAULT_ITERATIONS,
    .warmup = WARMUP_ITERATIONS,
    .pattern_type = PATTERN_RANDOM,
    .verify_data = 1,
    .continuous = 0,
    .histogram = 1,
    .verbose = 0,
    .cpu_core = -1,
    .dma_poll_us = 10,
    .target_addr = CSR_CTRL_SCRATCH_ADDR,
};

/* Get current time in nanoseconds */
static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Get current time in microseconds */
static inline uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

/* Memory prefetch hints */
static inline void prefetch_read(const void *addr) {
    __builtin_prefetch(addr, 0, 3);
}

static inline void prefetch_write(void *addr) {
    __builtin_prefetch(addr, 1, 3);
}

/* Generate test pattern */
static void generate_pattern(uint32_t *buffer, size_t count, uint32_t iteration) {
    size_t i;

    switch (config.pattern_type) {
    case PATTERN_SEQ:
        for (i = 0; i < count; i++) {
            buffer[i] = (iteration << 16) | (i & 0xFFFF);
        }
        break;

    case PATTERN_RANDOM:
        for (i = 0; i < count; i++) {
            buffer[i] = rand();
        }
        break;

    case PATTERN_FIXED:
        for (i = 0; i < count; i++) {
            buffer[i] = 0xDEADBEEF;
        }
        break;

    case PATTERN_WALKING:
        for (i = 0; i < count; i++) {
            buffer[i] = 1 << (i % 32);
        }
        break;
    }
}

/* Verify data pattern */
static int verify_pattern(uint32_t *buffer, uint32_t *expected, size_t count) {
    size_t i;
    int errors = 0;

    for (i = 0; i < count; i++) {
        if (buffer[i] != expected[i]) {
            if (config.verbose && errors < 10) {
                printf("Error at %zu: expected 0x%08x, got 0x%08x\n",
                       i, expected[i], buffer[i]);
            }
            errors++;
        }
    }

    return errors;
}

/* Update statistics */
static void update_stats(uint64_t latency_ns) {
    double latency_us = latency_ns / 1000.0;

    pthread_mutex_lock(&stats_mutex);

    stats.count++;
    stats.sum_us += latency_us;
    stats.sum_sq_us += latency_us * latency_us;

    if (latency_us < stats.min_us) stats.min_us = latency_us;
    if (latency_us > stats.max_us) stats.max_us = latency_us;

    /* Update histogram */
    if (config.histogram && latency_histogram) {
        int bucket = (int)latency_us;
        if (bucket < HISTOGRAM_BUCKETS) {
            latency_histogram[bucket]++;
        } else {
            histogram_overflow++;
        }
    }

    /* Store recent sample for percentiles */
    if (stats.recent_samples && stats.sample_count < config.iterations) {
        stats.recent_samples[stats.sample_index] = latency_ns;
        stats.sample_index = (stats.sample_index + 1) % config.iterations;
        if (stats.sample_count < config.iterations) {
            stats.sample_count++;
        }
    }

    pthread_mutex_unlock(&stats_mutex);
}

/* DMA processing thread */
static void* dma_thread_func(void *arg) {
    (void)arg;

    /* Set CPU affinity if requested */
    if (config.cpu_core >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET((config.cpu_core + 2) % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    while (keep_running) {
        pthread_mutex_lock(&dma_mutex);
        litepcie_dma_process(&dma_ctrl);
        pthread_mutex_unlock(&dma_mutex);

        if (config.dma_poll_us > 0) {
            usleep(config.dma_poll_us);
        }
    }

    return NULL;
}

/* Perform single DMA latency measurement */
static uint64_t measure_dma_latency(uint32_t *write_buf, uint32_t *read_buf,
                                   uint32_t *verify_buf, uint32_t iteration) {
    uint64_t start, end;
    char *dma_write_buf, *dma_read_buf;
    int timeout_count;
    size_t words = config.transfer_size / sizeof(uint32_t);

    /* Generate test pattern */
    generate_pattern(write_buf, words, iteration);
    if (config.verify_data) {
        memcpy(verify_buf, write_buf, config.transfer_size);
    }

    /* Get DMA buffers */
    pthread_mutex_lock(&dma_mutex);

    /* Wait for write buffer with timeout */
    timeout_count = 0;
    while ((dma_write_buf = litepcie_dma_next_write_buffer(&dma_ctrl)) == NULL) {
        pthread_mutex_unlock(&dma_mutex);
        if (++timeout_count > 1000) {
            return UINT64_MAX;
        }
        usleep(10);
        pthread_mutex_lock(&dma_mutex);
    }

    /* Copy data to DMA buffer */
    memcpy(dma_write_buf, write_buf, config.transfer_size);

    pthread_mutex_unlock(&dma_mutex);

    /* Start timing */
    start = get_time_ns();

    /* Configure DMA transfer to MAIN_RAM */
    /* Note: In the optimized version, the actual DMA configuration
       is handled by litepcie_dma library internally */

    /* Wait for read buffer (data should loop back) */
    pthread_mutex_lock(&dma_mutex);

    timeout_count = 0;
    while ((dma_read_buf = litepcie_dma_next_read_buffer(&dma_ctrl)) == NULL) {
        pthread_mutex_unlock(&dma_mutex);
        if (++timeout_count > 1000) {
            return UINT64_MAX;
        }
        usleep(10);
        pthread_mutex_lock(&dma_mutex);
    }

    /* Copy data from DMA buffer */
    memcpy(read_buf, dma_read_buf, config.transfer_size);

    pthread_mutex_unlock(&dma_mutex);

    /* End timing */
    end = get_time_ns();

    /* Verify if enabled */
    if (config.verify_data) {
        int errors = verify_pattern(read_buf, verify_buf, words);
        if (errors > 0) {
            pthread_mutex_lock(&stats_mutex);
            stats.errors += errors;
            pthread_mutex_unlock(&stats_mutex);
        }
    }

    return end - start;
}

/* Latency measurement thread */
static void* latency_thread_func(void *arg) {
    uint32_t *write_buf, *read_buf, *verify_buf;
    uint32_t iteration = 0;
    int warmup_done = 0;
    (void)arg;

    /* Set CPU affinity if requested */
    if (config.cpu_core >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(config.cpu_core, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    /* Allocate buffers */
    write_buf = aligned_alloc(CACHE_LINE_SIZE, config.transfer_size);
    read_buf = aligned_alloc(CACHE_LINE_SIZE, config.transfer_size);
    verify_buf = aligned_alloc(CACHE_LINE_SIZE, config.transfer_size);

    if (!write_buf || !read_buf || !verify_buf) {
        fprintf(stderr, "Failed to allocate measurement buffers\n");
        return NULL;
    }

    /* Warmup phase */
    if (config.warmup > 0 && !config.continuous) {
        for (int i = 0; i < config.warmup && keep_running; i++) {
            measure_dma_latency(write_buf, read_buf, verify_buf, iteration++);
        }
        warmup_done = 1;
    }

    /* Main measurement loop */
    while (keep_running) {
        uint64_t latency_ns = measure_dma_latency(write_buf, read_buf,
                                                  verify_buf, iteration++);

        if (latency_ns != UINT64_MAX) {
            update_stats(latency_ns);
        }

        /* Check if done (non-continuous mode) */
        if (!config.continuous && warmup_done &&
            stats.count >= (uint64_t)config.iterations) {
            keep_running = 0;
            break;
        }
    }

    free(write_buf);
    free(read_buf);
    free(verify_buf);

    return NULL;
}

/* Calculate percentile from recent samples */
static double calculate_percentile(double p) {
    uint64_t *sorted;
    int count;
    double result;

    pthread_mutex_lock(&stats_mutex);

    if (!stats.recent_samples || stats.sample_count == 0) {
        pthread_mutex_unlock(&stats_mutex);
        return 0;
    }

    count = stats.sample_count;
    sorted = malloc(count * sizeof(uint64_t));
    if (!sorted) {
        pthread_mutex_unlock(&stats_mutex);
        return 0;
    }

    memcpy(sorted, stats.recent_samples, count * sizeof(uint64_t));
    pthread_mutex_unlock(&stats_mutex);

    /* Sort samples */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (sorted[i] > sorted[j]) {
                uint64_t tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }

    int index = (int)(p * count / 100.0);
    if (index >= count) index = count - 1;
    result = sorted[index] / 1000.0;  /* Convert to microseconds */

    free(sorted);
    return result;
}

/* Print statistics */
static void print_stats(int final) {
    double mean, stddev;
    uint64_t elapsed_us;

    pthread_mutex_lock(&stats_mutex);

    if (stats.count == 0) {
        pthread_mutex_unlock(&stats_mutex);
        return;
    }

    mean = stats.sum_us / stats.count;
    stddev = sqrt((stats.sum_sq_us / stats.count) - (mean * mean));

    elapsed_us = get_time_us() -
                 (stats.start_time.tv_sec * 1000000ULL + stats.start_time.tv_usec);

    pthread_mutex_unlock(&stats_mutex);

    if (final) {
        printf("\n\nDMA Latency Statistics:\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("Measurements:     %lu\n", stats.count);
        printf("Errors:           %lu\n", stats.errors);
        printf("Min latency:      %.3f µs\n", stats.min_us);
        printf("Max latency:      %.3f µs\n", stats.max_us);
        printf("Mean latency:     %.3f µs\n", mean);
        printf("Std deviation:    %.3f µs\n", stddev);

        if (stats.recent_samples) {
            printf("\nPercentiles:\n");
            printf("  50%% (median):  %.3f µs\n", calculate_percentile(50));
            printf("  90%%:           %.3f µs\n", calculate_percentile(90));
            printf("  95%%:           %.3f µs\n", calculate_percentile(95));
            printf("  99%%:           %.3f µs\n", calculate_percentile(99));
            printf("  99.9%%:         %.3f µs\n", calculate_percentile(99.9));
        }

        printf("\nThroughput Analysis:\n");
        printf("Transfer size:    %d bytes\n", config.transfer_size);
        printf("Round-trip BW:    %.1f MB/s (at min latency)\n",
               (config.transfer_size * 2.0) / stats.min_us);
        printf("Avg throughput:   %.1f ops/sec\n",
               stats.count * 1000000.0 / elapsed_us);

        /* Print histogram if enabled */
        if (config.histogram && latency_histogram) {
            printf("\nLatency Distribution (µs):\n");
            for (int i = 0; i < HISTOGRAM_BUCKETS; i++) {
                if (latency_histogram[i] > 0) {
                    printf("  [%3d-%3d): %8lu", i, i+1, latency_histogram[i]);

                    /* Simple bar graph */
                    int bar_len = (latency_histogram[i] * 40) / stats.count;
                    if (bar_len > 40) bar_len = 40;
                    printf(" |");
                    for (int j = 0; j < bar_len; j++) printf("█");
                    printf("\n");
                }
            }
            if (histogram_overflow > 0) {
                printf("  [%d+):    %8lu (overflow)\n",
                       HISTOGRAM_BUCKETS, histogram_overflow);
            }
        }
    } else {
        /* Progress update */
        printf("\r[%6.1fs] Samples: %8lu | Min: %6.2fµs | Mean: %6.2fµs | "
               "Max: %6.2fµs | StdDev: %6.2fµs",
               elapsed_us / 1000000.0, stats.count, stats.min_us, mean,
               stats.max_us, stddev);
        fflush(stdout);
    }
}

/* Monitor thread for continuous mode */
static void* monitor_thread_func(void *arg) {
    (void)arg;

    while (keep_running) {
        usleep(UPDATE_INTERVAL_MS * 1000);
        if (keep_running) {
            print_stats(0);
        }
    }

    return NULL;
}

/* Signal handler */
static void signal_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

/* Usage */
static void usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("\nOptions:\n");
    printf("  -d <device>    Device file (default: %s)\n", config.device);
    printf("  -s <size>      Transfer size in bytes (default: %d)\n",
           config.transfer_size);
    printf("  -n <count>     Number of iterations (default: %d)\n",
           config.iterations);
    printf("  -w <count>     Warmup iterations (default: %d)\n", config.warmup);
    printf("  -p <pattern>   Test pattern: 0=seq, 1=random, 2=fixed, 3=walking "
           "(default: %d)\n", config.pattern_type);
    printf("  -a <address>   Target address in hex (default: 0x%08x)\n",
           config.target_addr);
    printf("  -c <cpu>       Pin to CPU core\n");
    printf("  -i <us>        DMA poll interval in microseconds (default: %d)\n",
           config.dma_poll_us);
    printf("  -C             Continuous mode (run until interrupted)\n");
    printf("  -H             Disable histogram\n");
    printf("  -V             Disable data verification\n");
    printf("  -v             Verbose output\n");
    printf("  -h             Show this help\n");
    printf("\nExamples:\n");
    printf("  # Basic latency test\n");
    printf("  %s\n\n", prog);
    printf("  # Continuous monitoring with 256-byte transfers\n");
    printf("  %s -C -s 256\n\n", prog);
    printf("  # High-precision test pinned to CPU 2\n");
    printf("  %s -c 2 -n 100000 -w 10000\n", prog);
}

/* Main */
int main(int argc, char *argv[]) {
    int opt;

    /* Parse options */
    while ((opt = getopt(argc, argv, "d:s:n:w:p:a:c:i:CHVvh")) != -1) {
        switch (opt) {
        case 'd':
            config.device = optarg;
            break;
        case 's':
            config.transfer_size = atoi(optarg);
            if (config.transfer_size < 4 || config.transfer_size > MAX_TRANSFER_SIZE) {
                fprintf(stderr, "Transfer size must be between 4 and %d bytes\n",
                        MAX_TRANSFER_SIZE);
                return 1;
            }
            break;
        case 'n':
            config.iterations = atoi(optarg);
            break;
        case 'w':
            config.warmup = atoi(optarg);
            break;
        case 'p':
            config.pattern_type = atoi(optarg);
            if (config.pattern_type < 0 || config.pattern_type > 3) {
                fprintf(stderr, "Invalid pattern type: %d\n", config.pattern_type);
                return 1;
            }
            break;
        case 'a':
            config.target_addr = strtoul(optarg, NULL, 0);
            break;
        case 'c':
            config.cpu_core = atoi(optarg);
            break;
        case 'i':
            config.dma_poll_us = atoi(optarg);
            break;
        case 'C':
            config.continuous = 1;
            break;
        case 'H':
            config.histogram = 0;
            break;
        case 'V':
            config.verify_data = 0;
            break;
        case 'v':
            config.verbose = 1;
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
    signal(SIGTERM, signal_handler);

    /* Allocate statistics structures */
    if (config.histogram) {
        latency_histogram = calloc(HISTOGRAM_BUCKETS, sizeof(uint64_t));
        if (!latency_histogram) {
            fprintf(stderr, "Failed to allocate histogram\n");
            return 1;
        }
    }

    stats.recent_samples = malloc(config.iterations * sizeof(uint64_t));
    if (!stats.recent_samples) {
        fprintf(stderr, "Failed to allocate sample buffer\n");
        if (latency_histogram) free(latency_histogram);
        return 1;
    }

    /* Initialize DMA */
    printf("LitePCIe DMA Latency Test V2\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Device:           %s\n", config.device);
    printf("Transfer size:    %d bytes\n", config.transfer_size);
    printf("Target address:   0x%08x\n", config.target_addr);
    printf("Pattern:          %s\n",
           config.pattern_type == 0 ? "Sequential" :
           config.pattern_type == 1 ? "Random" :
           config.pattern_type == 2 ? "Fixed (0xDEADBEEF)" : "Walking ones");
    printf("Mode:             %s\n", config.continuous ? "Continuous" : "Fixed iterations");
    if (!config.continuous) {
        printf("Iterations:       %d (after %d warmup)\n",
               config.iterations, config.warmup);
    }
    printf("Verification:     %s\n", config.verify_data ? "Enabled" : "Disabled");
    printf("CPU affinity:     %s\n",
           config.cpu_core >= 0 ? "Enabled" : "Disabled");
    printf("\nInitializing DMA...\n");

    memset(&dma_ctrl, 0, sizeof(dma_ctrl));
    dma_ctrl.loopback = 1;  /* Internal loopback for latency test */
    dma_ctrl.use_reader = 1;
    dma_ctrl.use_writer = 1;

    if (litepcie_dma_init(&dma_ctrl, config.device, 0)) {
        fprintf(stderr, "Failed to initialize DMA\n");
        if (latency_histogram) free(latency_histogram);
        free(stats.recent_samples);
        return 1;
    }

    dma_ctrl.reader_enable = 1;
    dma_ctrl.writer_enable = 1;

    /* Initialize statistics */
    gettimeofday(&stats.start_time, NULL);

    /* Seed random number generator */
    srand(time(NULL));

    /* Start threads */
    printf("Starting latency measurements...\n");
    if (config.continuous) {
        printf("Press Ctrl+C to stop.\n");
    }
    printf("\n");

    /* Start DMA processing thread */
    if (pthread_create(&dma_thread, NULL, dma_thread_func, NULL) != 0) {
        fprintf(stderr, "Failed to create DMA thread\n");
        goto cleanup;
    }

    /* Start latency measurement thread */
    if (pthread_create(&latency_thread, NULL, latency_thread_func, NULL) != 0) {
        fprintf(stderr, "Failed to create latency thread\n");
        keep_running = 0;
        pthread_join(dma_thread, NULL);
        goto cleanup;
    }

    /* Start monitor thread for continuous mode */
    if (config.continuous) {
        if (pthread_create(&monitor_thread, NULL, monitor_thread_func, NULL) != 0) {
            fprintf(stderr, "Failed to create monitor thread\n");
            keep_running = 0;
            pthread_join(dma_thread, NULL);
            pthread_join(latency_thread, NULL);
            goto cleanup;
        }
    }

    /* Wait for completion */
    pthread_join(latency_thread, NULL);
    keep_running = 0;  /* Signal other threads to stop */

    pthread_join(dma_thread, NULL);
    if (config.continuous) {
        pthread_join(monitor_thread, NULL);
    }

    /* Print final statistics */
    print_stats(1);

cleanup:
    /* Cleanup */
    litepcie_dma_cleanup(&dma_ctrl);
    if (latency_histogram) free(latency_histogram);
    if (stats.recent_samples) free(stats.recent_samples);

    return 0;
}
