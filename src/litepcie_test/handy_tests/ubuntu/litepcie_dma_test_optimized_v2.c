/*
 * Optimized LitePCIe DMA Test V2 - Fixed TX throughput
 * 
 * Improvements:
 * - More frequent DMA processing for better TX throughput
 * - Reduced polling timeout
 * - Better buffer management
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
#include <sched.h>
#include <sys/time.h>

#include "litepcie.h"
#include "liblitepcie.h"
#include "litepcie_dma.h"

/* Buffer configuration */
#define DMA_BUFFER_SIZE    8192
#define DMA_BUFFER_COUNT   256
#define BATCH_SIZE         16
#define CACHE_LINE_SIZE    64

/* Test patterns */
#define PATTERN_SEQ        0
#define PATTERN_RANDOM     1
#define PATTERN_ONES       2
#define PATTERN_ZEROS      3
#define PATTERN_ALT        4

/* Global state */
static volatile int keep_running = 1;
static struct litepcie_dma_ctrl dma_ctrl;
static pthread_t reader_thread, writer_thread, dma_thread;
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t dma_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t global_seed = 0;

/* Statistics */
typedef struct {
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_buffers;
    uint64_t rx_buffers;
    uint64_t errors;
    uint64_t dma_calls;
    struct timeval start_time;
} dma_stats_t;

static dma_stats_t stats = {0};

/* Configuration */
typedef struct {
    int pattern_type;
    int data_width;
    int verify_data;
    int verbose;
    int cpu_affinity;
    int poll_interval_us;
} dma_config_t;

static dma_config_t config = {
    .pattern_type = PATTERN_RANDOM,
    .data_width = 32,
    .verify_data = 1,
    .verbose = 0,
    .cpu_affinity = 1,
    .poll_interval_us = 100  /* 100 microseconds instead of 100ms */
};

/* Memory prefetch hints */
static inline void prefetch_read(const void *addr) {
    __builtin_prefetch(addr, 0, 3);
}

static inline void prefetch_write(void *addr) {
    __builtin_prefetch(addr, 1, 3);
}

/* Get current time in microseconds */
static inline uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

/* Generate test pattern */
static void generate_pattern(uint32_t *buffer, size_t count, uint32_t *seed) {
    size_t i;
    
    switch (config.pattern_type) {
    case PATTERN_SEQ:
        for (i = 0; i < count; i++) {
            buffer[i] = i & 0xFFFFFFFF;
        }
        break;
        
    case PATTERN_RANDOM:
        for (i = 0; i < count; i++) {
            *seed = *seed * 69069 + 1;
            buffer[i] = *seed;
        }
        break;
        
    case PATTERN_ONES:
        memset(buffer, 0xFF, count * sizeof(uint32_t));
        break;
        
    case PATTERN_ZEROS:
        memset(buffer, 0, count * sizeof(uint32_t));
        break;
        
    case PATTERN_ALT:
        for (i = 0; i < count; i++) {
            buffer[i] = (i & 1) ? 0xFFFFFFFF : 0x00000000;
        }
        break;
    }
}

/* Verify data pattern */
static int verify_pattern(uint32_t *buffer, size_t count, uint32_t *seed) {
    size_t i;
    uint32_t expected;
    int errors = 0;
    
    switch (config.pattern_type) {
    case PATTERN_SEQ:
        for (i = 0; i < count; i++) {
            expected = i & 0xFFFFFFFF;
            if (buffer[i] != expected) {
                if (config.verbose) {
                    printf("Error at %zu: expected 0x%08x, got 0x%08x\n", 
                           i, expected, buffer[i]);
                }
                errors++;
                if (errors > 10) break;
            }
        }
        break;
        
    case PATTERN_RANDOM:
        for (i = 0; i < count; i++) {
            *seed = *seed * 69069 + 1;
            expected = *seed;
            if (buffer[i] != expected) {
                if (config.verbose) {
                    printf("Error at %zu: expected 0x%08x, got 0x%08x\n", 
                           i, expected, buffer[i]);
                }
                errors++;
                if (errors > 10) break;
            }
        }
        break;
        
    case PATTERN_ONES:
        for (i = 0; i < count; i++) {
            if (buffer[i] != 0xFFFFFFFF) {
                errors++;
                if (errors > 10) break;
            }
        }
        break;
        
    case PATTERN_ZEROS:
        for (i = 0; i < count; i++) {
            if (buffer[i] != 0) {
                errors++;
                if (errors > 10) break;
            }
        }
        break;
        
    case PATTERN_ALT:
        for (i = 0; i < count; i++) {
            expected = (i & 1) ? 0xFFFFFFFF : 0x00000000;
            if (buffer[i] != expected) {
                errors++;
                if (errors > 10) break;
            }
        }
        break;
    }
    
    return errors;
}

/* DMA processing thread - handles litepcie_dma_process calls */
static void* dma_thread_func(void *arg) {
    (void)arg;
    
    /* Set CPU affinity if requested */
    if (config.cpu_affinity) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(2, &cpuset);  /* Use core 2 for DMA processing */
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }
    
    while (keep_running) {
        pthread_mutex_lock(&dma_mutex);
        litepcie_dma_process(&dma_ctrl);
        pthread_mutex_unlock(&dma_mutex);
        
        pthread_mutex_lock(&stats_mutex);
        stats.dma_calls++;
        pthread_mutex_unlock(&stats_mutex);
        
        usleep(config.poll_interval_us);
    }
    
    return NULL;
}

/* Writer thread - generates and sends data */
static void* writer_thread_func(void *arg) {
    uint32_t seed = global_seed;
    int i;
    int consecutive_empty = 0;
    (void)arg;
    
    /* Set CPU affinity if requested */
    if (config.cpu_affinity) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(0, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }
    
    /* Pre-generate pattern buffer for efficiency */
    uint32_t *pattern = malloc(DMA_BUFFER_SIZE);
    if (!pattern) {
        fprintf(stderr, "Failed to allocate pattern buffer\n");
        return NULL;
    }
    generate_pattern(pattern, DMA_BUFFER_SIZE / sizeof(uint32_t), &seed);
    
    while (keep_running) {
        pthread_mutex_lock(&dma_mutex);
        char *buf = litepcie_dma_next_write_buffer(&dma_ctrl);
        pthread_mutex_unlock(&dma_mutex);
        
        if (buf) {
            consecutive_empty = 0;
            
            /* Copy pattern with prefetching */
            if (config.data_width == 32) {
                uint32_t *dst = (uint32_t*)buf;
                uint32_t *src = pattern;
                size_t words = DMA_BUFFER_SIZE / sizeof(uint32_t);
                
                for (i = 0; i < words; i += 16) {
                    if (i + 64 < words) {
                        prefetch_read(src + i + 64);
                        prefetch_write(dst + i + 64);
                    }
                    memcpy(dst + i, src + i, 16 * sizeof(uint32_t));
                }
            } else {
                /* Handle other data widths */
                memcpy(buf, pattern, DMA_BUFFER_SIZE);
            }
            
            /* Update statistics */
            pthread_mutex_lock(&stats_mutex);
            stats.tx_bytes += DMA_BUFFER_SIZE;
            stats.tx_buffers++;
            pthread_mutex_unlock(&stats_mutex);
        } else {
            consecutive_empty++;
            /* If no buffers for a while, yield CPU */
            if (consecutive_empty > 10) {
                usleep(1);
            }
        }
    }
    
    free(pattern);
    return NULL;
}

/* Reader thread - receives and verifies data */
static void* reader_thread_func(void *arg) {
    uint32_t seed = global_seed;
    int consecutive_empty = 0;
    (void)arg;
    
    /* Set CPU affinity if requested */
    if (config.cpu_affinity) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(1, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }
    
    while (keep_running) {
        pthread_mutex_lock(&dma_mutex);
        char *buf = litepcie_dma_next_read_buffer(&dma_ctrl);
        pthread_mutex_unlock(&dma_mutex);
        
        if (buf) {
            consecutive_empty = 0;
            
            /* Verify data if enabled */
            if (config.verify_data && config.pattern_type <= PATTERN_ALT) {
                int errors = verify_pattern((uint32_t*)buf, 
                                           DMA_BUFFER_SIZE / sizeof(uint32_t), 
                                           &seed);
                if (errors > 0) {
                    pthread_mutex_lock(&stats_mutex);
                    stats.errors += errors;
                    pthread_mutex_unlock(&stats_mutex);
                }
            }
            
            /* Update statistics */
            pthread_mutex_lock(&stats_mutex);
            stats.rx_bytes += DMA_BUFFER_SIZE;
            stats.rx_buffers++;
            pthread_mutex_unlock(&stats_mutex);
        } else {
            consecutive_empty++;
            /* If no buffers for a while, yield CPU */
            if (consecutive_empty > 10) {
                usleep(1);
            }
        }
    }
    
    return NULL;
}

/* Signal handler */
static void signal_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

/* Print statistics */
static void print_stats(void) {
    uint64_t now = get_time_us();
    uint64_t elapsed_us;
    double elapsed_s;
    double tx_gbps, rx_gbps;
    
    pthread_mutex_lock(&stats_mutex);
    
    elapsed_us = now - (stats.start_time.tv_sec * 1000000ULL + stats.start_time.tv_usec);
    elapsed_s = elapsed_us / 1000000.0;
    
    if (elapsed_s > 0) {
        tx_gbps = (stats.tx_bytes * 8.0) / (elapsed_s * 1e9);
        rx_gbps = (stats.rx_bytes * 8.0) / (elapsed_s * 1e9);
        
        printf("\r[%6.2fs] TX: %8.3f Gbps (%lu buffers) | RX: %8.3f Gbps (%lu buffers) | Errors: %lu | DMA: %lu/s",
               elapsed_s, tx_gbps, stats.tx_buffers, rx_gbps, stats.rx_buffers, stats.errors,
               (uint64_t)(stats.dma_calls / elapsed_s));
        fflush(stdout);
    }
    
    pthread_mutex_unlock(&stats_mutex);
}

/* Usage */
static void usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -d <device>    Device file (default: /dev/litepcie0)\n");
    printf("  -p <pattern>   Pattern: 0=seq, 1=random, 2=ones, 3=zeros, 4=alt (default: 1)\n");
    printf("  -w <width>     Data width in bits (default: 32)\n");
    printf("  -l             Enable external loopback (default: internal)\n");
    printf("  -z             Enable zero-copy mode\n");
    printf("  -n             Disable data verification\n");
    printf("  -a             Disable CPU affinity\n");
    printf("  -i <us>        DMA poll interval in microseconds (default: 100)\n");
    printf("  -v             Verbose output\n");
    printf("  -t <seconds>   Test duration (0 = infinite)\n");
    printf("  -h             Show this help\n");
}

/* Main */
int main(int argc, char *argv[]) {
    const char *device = "/dev/litepcie0";
    int opt;
    int duration = 0;
    uint8_t zero_copy = 0;
    uint8_t external_loopback = 0;
    
    /* Parse options */
    while ((opt = getopt(argc, argv, "d:p:w:lznai:vt:h")) != -1) {
        switch (opt) {
        case 'd':
            device = optarg;
            break;
        case 'p':
            config.pattern_type = atoi(optarg);
            if (config.pattern_type < 0 || config.pattern_type > 4) {
                fprintf(stderr, "Invalid pattern type: %d\n", config.pattern_type);
                return 1;
            }
            break;
        case 'w':
            config.data_width = atoi(optarg);
            if (config.data_width < 1 || config.data_width > 32) {
                fprintf(stderr, "Invalid data width: %d\n", config.data_width);
                return 1;
            }
            break;
        case 'l':
            external_loopback = 1;
            break;
        case 'z':
            zero_copy = 1;
            break;
        case 'n':
            config.verify_data = 0;
            break;
        case 'a':
            config.cpu_affinity = 0;
            break;
        case 'i':
            config.poll_interval_us = atoi(optarg);
            if (config.poll_interval_us < 1 || config.poll_interval_us > 100000) {
                fprintf(stderr, "Invalid poll interval: %d\n", config.poll_interval_us);
                return 1;
            }
            break;
        case 'v':
            config.verbose = 1;
            break;
        case 't':
            duration = atoi(optarg);
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
    
    /* Initialize DMA */
    printf("Initializing DMA with %s loopback...\n", 
           external_loopback ? "external" : "internal");
    
    memset(&dma_ctrl, 0, sizeof(dma_ctrl));
    dma_ctrl.loopback = external_loopback ? 0 : 1;
    dma_ctrl.use_reader = 1;
    dma_ctrl.use_writer = 1;
    
    if (litepcie_dma_init(&dma_ctrl, device, zero_copy)) {
        fprintf(stderr, "Failed to initialize DMA\n");
        return 1;
    }
    
    dma_ctrl.reader_enable = 1;
    dma_ctrl.writer_enable = 1;
    
    /* Initialize statistics */
    gettimeofday(&stats.start_time, NULL);
    
    /* Initialize global seed for pattern generation */
    global_seed = time(NULL);
    
    /* Disable verification in internal loopback mode by default */
    if (!external_loopback && config.verify_data) {
        printf("Note: Disabling data verification in internal loopback mode.\n");
        printf("Use -l for external loopback to verify data integrity.\n");
        config.verify_data = 0;
    }
    
    /* Start worker threads */
    printf("Starting optimized DMA test V2...\n");
    printf("Pattern: %s, Data width: %d bits, Zero-copy: %s\n",
           config.pattern_type == 0 ? "Sequential" :
           config.pattern_type == 1 ? "Random" :
           config.pattern_type == 2 ? "All ones" :
           config.pattern_type == 3 ? "All zeros" : "Alternating",
           config.data_width,
           zero_copy ? "enabled" : "disabled");
    printf("CPU affinity: %s, Poll interval: %d µs, Verification: %s\n",
           config.cpu_affinity ? "enabled" : "disabled",
           config.poll_interval_us,
           config.verify_data ? "enabled" : "disabled");
    printf("Press Ctrl+C to stop.\n\n");
    
    /* Start DMA processing thread */
    if (pthread_create(&dma_thread, NULL, dma_thread_func, NULL) != 0) {
        fprintf(stderr, "Failed to create DMA thread\n");
        goto cleanup;
    }
    
    /* Start writer thread */
    if (pthread_create(&writer_thread, NULL, writer_thread_func, NULL) != 0) {
        fprintf(stderr, "Failed to create writer thread\n");
        keep_running = 0;
        pthread_join(dma_thread, NULL);
        goto cleanup;
    }
    
    /* Start reader thread */
    if (pthread_create(&reader_thread, NULL, reader_thread_func, NULL) != 0) {
        fprintf(stderr, "Failed to create reader thread\n");
        keep_running = 0;
        pthread_join(dma_thread, NULL);
        pthread_join(writer_thread, NULL);
        goto cleanup;
    }
    
    /* Monitor loop */
    uint64_t end_time = duration > 0 ? get_time_us() + duration * 1000000ULL : 0;
    
    while (keep_running) {
        usleep(200000); /* 200ms update interval */
        print_stats();
        
        if (duration > 0 && get_time_us() >= end_time) {
            keep_running = 0;
        }
    }
    
    printf("\n\nStopping test...\n");
    
    /* Wait for threads */
    pthread_join(dma_thread, NULL);
    pthread_join(writer_thread, NULL);
    pthread_join(reader_thread, NULL);
    
    /* Final statistics */
    print_stats();
    printf("\n");
    
cleanup:
    /* Cleanup */
    litepcie_dma_cleanup(&dma_ctrl);
    
    return 0;
}