/* SPDX-License-Identifier: BSD-2-Clause
 *
 * LitePCIe simple latency test
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
#include "liblitepcie.h"

/* Variables */
/*-----------*/

sig_atomic_t keep_running = 1;

void intHandler(int dummy) {
    keep_running = 0;
}

/* Get current time in microseconds */
static inline uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

/* Simple Loopback Test */
/*----------------------*/

static void simple_loopback_test(const char *device_name, uint32_t iterations)
{
    static struct litepcie_dma_ctrl dma = {.use_reader = 1, .use_writer = 1, .loopback = 1};
    
    uint64_t min_latency = UINT64_MAX;
    uint64_t max_latency = 0;
    uint64_t total_latency = 0;
    uint32_t successful = 0;
    
    printf("\nStarting simple PCIe loopback test:\n");
    printf("- Device: %s\n", device_name);
    printf("- Iterations: %u\n", iterations);
    printf("\nInitializing DMA...\n");

    /* Initialize DMA with loopback enabled */
    if (litepcie_dma_init(&dma, device_name, 0)) {
        printf("Failed to initialize DMA\n");
        exit(1);
    }

    /* Enable DMA */
    dma.reader_enable = 1;
    dma.writer_enable = 1;

    printf("DMA initialized. Starting test...\n\n");

    /* Let DMA stabilize */
    for (int i = 0; i < 10; i++) {
        litepcie_dma_process(&dma);
        usleep(1000);
    }

    /* Main test loop */
    for (uint32_t iter = 0; iter < iterations && keep_running; iter++) {
        uint64_t start_time, end_time;
        char *buf_wr = NULL;
        char *buf_rd = NULL;
        uint32_t test_value = 0xDEADBEEF + iter;
        
        /* Wait for write buffer */
        int retries = 100;
        while (retries > 0 && !buf_wr) {
            litepcie_dma_process(&dma);
            buf_wr = litepcie_dma_next_write_buffer(&dma);
            if (!buf_wr) {
                usleep(100);
                retries--;
            }
        }
        
        if (!buf_wr) {
            printf("Warning: No write buffer available at iteration %u\n", iter);
            continue;
        }

        /* Fill buffer with test pattern */
        uint32_t *data = (uint32_t *)buf_wr;
        for (int i = 0; i < DMA_BUFFER_SIZE / sizeof(uint32_t); i++) {
            data[i] = test_value + i;
        }

        /* Record start time */
        start_time = get_time_us();

        /* Send buffer - increment sw_count to commit the buffer */
        dma.reader_sw_count++;

        /* Wait for data to loop back */
        retries = 1000;
        while (retries > 0 && !buf_rd && keep_running) {
            litepcie_dma_process(&dma);
            buf_rd = litepcie_dma_next_read_buffer(&dma);
            if (!buf_rd) {
                usleep(10);
                retries--;
            }
        }

        if (!buf_rd) {
            printf("Warning: Timeout waiting for read buffer at iteration %u\n", iter);
            continue;
        }

        /* Record end time */
        end_time = get_time_us();

        /* Verify data */
        uint32_t *read_data = (uint32_t *)buf_rd;
        int valid = 1;
        for (int i = 0; i < 16; i++) { /* Check first 16 words */
            if (read_data[i] != (test_value + i)) {
                printf("Data mismatch at iteration %u, word %d: expected 0x%08X, got 0x%08X\n",
                       iter, i, test_value + i, read_data[i]);
                valid = 0;
                break;
            }
        }

        /* Mark read buffer as consumed */
        dma.writer_sw_count++;

        if (valid) {
            uint64_t latency = end_time - start_time;
            if (latency < min_latency) min_latency = latency;
            if (latency > max_latency) max_latency = latency;
            total_latency += latency;
            successful++;
            
            if ((iter + 1) % 100 == 0) {
                printf("Progress: %u/%u iterations completed\n", iter + 1, iterations);
            }
        }
    }

    /* Print results */
    printf("\n\nTest Results:\n");
    printf("=============\n");
    printf("Successful iterations: %u / %u\n", successful, iterations);
    if (successful > 0) {
        printf("Min latency: %lu us\n", min_latency);
        printf("Max latency: %lu us\n", max_latency);
        printf("Avg latency: %.2f us\n", (double)total_latency / successful);
        printf("Throughput: %.2f MB/s\n", (double)DMA_BUFFER_SIZE / ((double)total_latency / successful));
    }

    /* Cleanup */
    dma.reader_enable = 0;
    dma.writer_enable = 0;
    litepcie_dma_cleanup(&dma);
}

/* Help */
/*------*/

static void help(void)
{
    printf("LitePCIe simple latency test\n"
           "usage: litepcie_latency_test_simple [options]\n"
           "\n"
           "options:\n"
           "-h                    Help\n"
           "-c device_num         Select the device (default = 0)\n"
           "-i iterations         Number of iterations (default = 1000)\n"
           "\n"
           "example:\n"
           "  litepcie_latency_test_simple -c 0 -i 1000\n"
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
    static uint32_t iterations = 1000;

    signal(SIGINT, intHandler);

    /* Parse arguments */
    for (;;) {
        c = getopt(argc, argv, "hc:i:");
        if (c == -1)
            break;
        switch(c) {
        case 'h':
            help();
            break;
        case 'c':
            litepcie_device_num = atoi(optarg);
            break;
        case 'i':
            iterations = strtoul(optarg, NULL, 0);
            break;
        default:
            help();
            exit(1);
        }
    }

    /* Select device */
    snprintf(litepcie_device, sizeof(litepcie_device), "/dev/litepcie%d", litepcie_device_num);

    /* Run test */
    simple_loopback_test(litepcie_device, iterations);

    return 0;
}