/* SPDX-License-Identifier: BSD-2-Clause
 *
 * LitePCIe latency test
 *
 * This file is part of LitePCIe.
 *
 * Copyright (C) 2024 - PCIe Loopback Latency Test
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>
#include "liblitepcie.h"

/* Variables */
/*-----------*/

sig_atomic_t keep_running = 1;

/* Statistics structure */
struct latency_stats {
    double min_latency_us;
    double max_latency_us;
    double avg_latency_us;
    double total_latency_us;
    uint64_t count;
};

void intHandler(int dummy) {
    keep_running = 0;
}

/* Get current time in microseconds */
static inline uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

/* Loopback Latency Test */
/*-----------------------*/

static void litepcie_loopback_latency(const char *device_name, uint32_t packet_size, uint32_t iterations, uint8_t zero_copy)
{
    static struct litepcie_dma_ctrl dma = {.use_reader = 1, .use_writer = 1, .loopback = 1};
    struct latency_stats stats = {
        .min_latency_us = 1e9,
        .max_latency_us = 0,
        .avg_latency_us = 0,
        .total_latency_us = 0,
        .count = 0
    };
    
    uint64_t start_time, end_time;
    double latency_us;
    uint64_t test_pattern = 0x1234567890ABCDEF;
    uint64_t *write_data, *read_data;
    size_t data_size;
    int i, j;

    /* Validate packet size */
    if (packet_size < sizeof(uint64_t)) {
        printf("Error: packet size must be at least %zu bytes\n", sizeof(uint64_t));
        return;
    }
    if (packet_size > DMA_BUFFER_SIZE) {
        printf("Error: packet size cannot exceed DMA buffer size (%d bytes)\n", DMA_BUFFER_SIZE);
        return;
    }

    /* Initialize DMA with loopback enabled */
    if (litepcie_dma_init(&dma, device_name, zero_copy)) {
        printf("Failed to initialize DMA\n");
        exit(1);
    }

    /* Enable DMA reader and writer */
    dma.reader_enable = 1;
    dma.writer_enable = 1;

    printf("\nStarting PCIe loopback latency test:\n");
    printf("- Device: %s\n", device_name);
    printf("- Packet size: %u bytes\n", packet_size);
    printf("- Iterations: %u\n", iterations);
    printf("- Zero-copy mode: %s\n", zero_copy ? "enabled" : "disabled");
    printf("\nRunning test...\n\n");

    /* Clear the DMA pipeline first */
    for (i = 0; i < 10; i++) {
        litepcie_dma_process(&dma);
        usleep(1000);
    }

    /* Main test loop */
    for (i = 0; i < iterations && keep_running; i++) {
        /* Fill write buffer with test pattern */
        data_size = packet_size / sizeof(uint64_t);
        
        /* Wait for write buffer */
        char *buf_wr = NULL;
        int retry = 100;
        while (retry > 0) {
            litepcie_dma_process(&dma);
            buf_wr = litepcie_dma_next_write_buffer(&dma);
            if (buf_wr)
                break;
            usleep(10);
            retry--;
        }
        
        if (!buf_wr) {
            printf("Warning: No write buffer available at iteration %d\n", i);
            continue;
        }

        /* Clear buffer first */
        memset(buf_wr, 0, DMA_BUFFER_SIZE);
        
        /* Write unique pattern for this iteration */
        write_data = (uint64_t *)buf_wr;
        write_data[0] = test_pattern + i; /* Unique ID at start */
        for (j = 1; j < data_size; j++) {
            write_data[j] = test_pattern ^ ((uint64_t)i << 32) ^ ((uint64_t)j);
        }

        /* Record start time just before sending */
        start_time = get_time_us();

        /* Commit write buffer - this sends the data */
        dma.reader_sw_count++;
        
        /* Process DMA to flush the write */
        litepcie_dma_process(&dma);

        /* Wait for loopback data */
        char *buf_rd = NULL;
        retry = 1000; /* 10ms timeout */
        while (retry > 0 && keep_running) {
            litepcie_dma_process(&dma);
            buf_rd = litepcie_dma_next_read_buffer(&dma);
            if (buf_rd) {
                /* Check if this is our packet by looking at the ID */
                read_data = (uint64_t *)buf_rd;
                if (read_data[0] == (test_pattern + i)) {
                    /* This is our packet */
                    break;
                } else {
                    /* Not our packet, consume it and continue */
                    dma.writer_sw_count++;
                    buf_rd = NULL;
                }
            }
            usleep(10);
            retry--;
        }

        if (!buf_rd) {
            printf("Warning: Timeout waiting for loopback data at iteration %d\n", i);
            continue;
        }

        /* Record end time */
        end_time = get_time_us();
        
        /* Verify received data */
        int data_valid = 1;
        for (j = 0; j < data_size; j++) {
            uint64_t expected = (j == 0) ? (test_pattern + i) : 
                               (test_pattern ^ ((uint64_t)i << 32) ^ ((uint64_t)j));
            if (read_data[j] != expected) {
                printf("Error: Data mismatch at iteration %d, offset %d: expected 0x%016lX, got 0x%016lX\n",
                       i, j, expected, read_data[j]);
                data_valid = 0;
                break;
            }
        }

        /* Consume read buffer */
        dma.writer_sw_count++;

        if (data_valid) {
            /* Calculate latency */
            latency_us = (double)(end_time - start_time);
            
            /* Update statistics */
            if (latency_us < stats.min_latency_us)
                stats.min_latency_us = latency_us;
            if (latency_us > stats.max_latency_us)
                stats.max_latency_us = latency_us;
            stats.total_latency_us += latency_us;
            stats.count++;

            /* Print progress every 100 iterations for small tests, 1000 for large */
            int progress_interval = (iterations <= 1000) ? 100 : 1000;
            if ((i + 1) % progress_interval == 0) {
                printf("Progress: %u/%u iterations completed (avg latency: %.2f us)\n", 
                       i + 1, iterations, stats.total_latency_us / stats.count);
            }
        }
    }

        /* Calculate average latency */
    if (stats.count > 0) {
        stats.avg_latency_us = stats.total_latency_us / stats.count;
    }

    /* Disable DMA */
    dma.reader_enable = 0;
    dma.writer_enable = 0;

    /* Print results */
    printf("\n\nLatency Test Results:\n");
    printf("=====================\n");
    printf("Successful iterations: %" PRIu64 " / %u\n", stats.count, iterations);
    if (stats.count > 0) {
        printf("Min latency: %.2f us\n", stats.min_latency_us);
        printf("Max latency: %.2f us\n", stats.max_latency_us);
        printf("Avg latency: %.2f us\n", stats.avg_latency_us);
        printf("Throughput: %.2f MB/s (based on avg latency)\n", 
               (double)packet_size / stats.avg_latency_us);
    } else {
        printf("No successful iterations completed.\n");
    }

    /* Cleanup DMA */
    litepcie_dma_cleanup(&dma);
}

/* Help */
/*------*/

static void help(void)
{
    printf("LitePCIe latency testing utility\n"
           "usage: litepcie_latency_test [options]\n"
           "\n"
           "options:\n"
           "-h                    Help\n"
           "-c device_num         Select the device (default = 0)\n"
           "-s packet_size        Packet size in bytes (default = 1024)\n"
           "-i iterations         Number of iterations (default = 10000)\n"
           "-z                    Enable zero-copy DMA mode\n"
           "\n"
           "example:\n"
           "  litepcie_latency_test -c 0 -s 4096 -i 10000\n"
           );
    exit(1);
}

/* Main */
/*------*/

int main(int argc, char **argv)
{
    int c;
    static char litepcie_device[1024];
    static int litepcie_device_num = 0;
    static uint32_t packet_size = 1024;
    static uint32_t iterations = 10000;
    static uint8_t litepcie_device_zero_copy = 0;

    signal(SIGINT, intHandler);

    /* Parse arguments */
    for (;;) {
        c = getopt(argc, argv, "hc:s:i:z");
        if (c == -1)
            break;
        switch(c) {
        case 'h':
            help();
            break;
        case 'c':
            litepcie_device_num = atoi(optarg);
            break;
        case 's':
            packet_size = strtoul(optarg, NULL, 0);
            break;
        case 'i':
            iterations = strtoul(optarg, NULL, 0);
            break;
        case 'z':
            litepcie_device_zero_copy = 1;
            break;
        default:
            help();
            exit(1);
        }
    }

    /* Select device */
    snprintf(litepcie_device, sizeof(litepcie_device), "/dev/litepcie%d", litepcie_device_num);

    /* Run loopback latency test */
    litepcie_loopback_latency(litepcie_device, packet_size, iterations, litepcie_device_zero_copy);

    return 0;
}