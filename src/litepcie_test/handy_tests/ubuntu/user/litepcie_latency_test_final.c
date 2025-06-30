/* SPDX-License-Identifier: BSD-2-Clause
 *
 * LitePCIe latency test with proper synchronization
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

/* Latency measurement structure */
struct latency_measurement {
    uint64_t timestamp;
    uint32_t sequence;
    uint32_t marker;
};

#define MAX_PENDING 256
#define MARKER_VALUE 0xCAFEBABE

void intHandler(int dummy) {
    keep_running = 0;
}

/* Get current time in microseconds */
static inline uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

/* PCIe Loopback Latency Test */
/*----------------------------*/

static void pcie_latency_test(const char *device_name, uint32_t iterations, uint32_t packet_size)
{
    static struct litepcie_dma_ctrl dma = {.use_reader = 1, .use_writer = 1, .loopback = 1};
    
    struct latency_measurement pending[MAX_PENDING];
    int pending_head = 0, pending_tail = 0;
    uint8_t *processed;  /* Track processed sequences */
    
    uint64_t min_latency = UINT64_MAX;
    uint64_t max_latency = 0;
    uint64_t total_latency = 0;
    uint32_t successful = 0;
    uint32_t sequence = 0;
    uint32_t duplicates = 0;
    
    printf("\nStarting PCIe loopback latency test:\n");
    printf("- Device: %s\n", device_name);
    printf("- Iterations: %u\n", iterations);
    printf("- Packet size: %u bytes\n", packet_size);
    printf("\nInitializing DMA...\n");

    /* Allocate and initialize arrays */
    processed = calloc(iterations, sizeof(uint8_t));
    if (!processed) {
        printf("Failed to allocate memory\n");
        exit(1);
    }
    memset(pending, 0, sizeof(pending));

    /* Initialize DMA with loopback enabled */
    if (litepcie_dma_init(&dma, device_name, 0)) {
        printf("Failed to initialize DMA\n");
        exit(1);
    }

    /* Enable DMA */
    dma.reader_enable = 1;
    dma.writer_enable = 1;

    printf("DMA initialized. Starting test...\n\n");

    /* Let DMA stabilize and flush any pending data */
    for (int i = 0; i < 100; i++) {
        litepcie_dma_process(&dma);
        
        /* Consume any pending read buffers */
        char *buf_rd;
        while ((buf_rd = litepcie_dma_next_read_buffer(&dma)) != NULL) {
            dma.writer_sw_count++;
        }
        
        usleep(1000);
    }

    /* Reset counters after flush */
    uint32_t sent = 0;
    uint32_t received = 0;

    /* Main test loop */
    while ((sent < iterations || successful < sent) && keep_running) {
        char *buf_wr = NULL;
        char *buf_rd = NULL;
        
        /* Process DMA */
        litepcie_dma_process(&dma);
        
        /* Send packets if we haven't sent all iterations yet */
        if (sent < iterations) {
            buf_wr = litepcie_dma_next_write_buffer(&dma);
            if (buf_wr) {
                /* Clear buffer */
                memset(buf_wr, 0, DMA_BUFFER_SIZE);
                
                /* Create packet with sequence number and timestamp */
                uint32_t *data = (uint32_t *)buf_wr;
                data[0] = MARKER_VALUE;
                data[1] = sequence;
                
                /* Fill rest of packet */
                for (int i = 2; i < packet_size / sizeof(uint32_t); i++) {
                    data[i] = sequence + i;
                }
                
                /* Record timestamp */
                uint64_t timestamp = get_time_us();
                
                /* Store in pending queue */
                int next_tail = (pending_tail + 1) % MAX_PENDING;
                if (next_tail != pending_head) {
                    pending[pending_tail].timestamp = timestamp;
                    pending[pending_tail].sequence = sequence;
                    pending[pending_tail].marker = MARKER_VALUE;
                    pending_tail = next_tail;
                }
                
                /* Send buffer */
                dma.reader_sw_count++;
                sequence++;
                sent++;
                
                if (sent % 100 == 0) {
                    printf("Sent %u packets...\n", sent);
                }
            }
        }
        
        /* Check for received packets */
        buf_rd = litepcie_dma_next_read_buffer(&dma);
        if (buf_rd) {
            uint32_t *data = (uint32_t *)buf_rd;
            
            /* Check if this is one of our packets */
            if (data[0] == MARKER_VALUE) {
                uint32_t rx_sequence = data[1];
                uint64_t rx_time = get_time_us();
                
                /* Check if this is a duplicate */
                if (rx_sequence < iterations && processed[rx_sequence]) {
                    duplicates++;
                } else if (rx_sequence < iterations) {
                    /* Mark as processed */
                    processed[rx_sequence] = 1;
                    
                    /* Find matching pending packet */
                    int found = 0;
                    int idx = pending_head;
                    while (idx != pending_tail) {
                        if (pending[idx].sequence == rx_sequence) {
                            /* Calculate latency */
                            uint64_t latency = rx_time - pending[idx].timestamp;
                            
                            /* Verify data integrity */
                            int valid = 1;
                            for (int i = 2; i < packet_size / sizeof(uint32_t); i++) {
                                if (data[i] != rx_sequence + i) {
                                    printf("Data corruption at seq %u, offset %d\n", rx_sequence, i);
                                    valid = 0;
                                    break;
                                }
                            }
                            
                            if (valid) {
                                if (latency < min_latency) min_latency = latency;
                                if (latency > max_latency) max_latency = latency;
                                total_latency += latency;
                                successful++;
                            }
                            
                            /* Mark as found */
                            found = 1;
                            break;
                        }
                        idx = (idx + 1) % MAX_PENDING;
                    }
                    
                    if (!found && rx_sequence < sent) {
                        printf("Warning: Received sequence %u not in pending queue\n", rx_sequence);
                    }
                }
                
                received++;
            }
            
            /* Mark buffer as consumed */
            dma.writer_sw_count++;
        }
        
        /* Small delay to prevent busy waiting */
        if (!buf_wr && !buf_rd) {
            usleep(10);
        }
    }

    /* Print results */
    printf("\n\nLatency Test Results:\n");
    printf("====================\n");
    printf("Packets sent: %u\n", sent);
    printf("Packets received: %u\n", received);
    printf("Successful measurements: %u\n", successful);
    printf("Duplicate packets: %u\n", duplicates);
    
    if (successful > 0) {
        printf("\nLatency Statistics:\n");
        printf("Min latency: %lu us\n", min_latency);
        printf("Max latency: %lu us\n", max_latency);
        printf("Avg latency: %.2f us\n", (double)total_latency / successful);
        
        double throughput_mbps = ((double)packet_size * 8.0) / ((double)total_latency / successful);
        printf("\nThroughput (based on avg latency): %.2f Mbps\n", throughput_mbps);
        printf("Bandwidth efficiency: %.2f MB/s\n", (double)packet_size / ((double)total_latency / successful));
    }

    /* Cleanup */
    dma.reader_enable = 0;
    dma.writer_enable = 0;
    litepcie_dma_cleanup(&dma);
    free(processed);
}

/* Help */
/*------*/

static void help(void)
{
    printf("LitePCIe latency test\n"
           "usage: litepcie_latency_test_final [options]\n"
           "\n"
           "options:\n"
           "-h                    Help\n"
           "-c device_num         Select the device (default = 0)\n"
           "-i iterations         Number of iterations (default = 1000)\n"
           "-s packet_size        Packet size in bytes (default = 1024)\n"
           "\n"
           "example:\n"
           "  litepcie_latency_test_final -c 0 -i 1000 -s 4096\n"
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
    static uint32_t packet_size = 1024;

    signal(SIGINT, intHandler);

    /* Parse arguments */
    for (;;) {
        c = getopt(argc, argv, "hc:i:s:");
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
        case 's':
            packet_size = strtoul(optarg, NULL, 0);
            if (packet_size < 8) packet_size = 8;
            if (packet_size > DMA_BUFFER_SIZE) packet_size = DMA_BUFFER_SIZE;
            break;
        default:
            help();
            exit(1);
        }
    }

    /* Select device */
    snprintf(litepcie_device, sizeof(litepcie_device), "/dev/litepcie%d", litepcie_device_num);

    /* Run test */
    pcie_latency_test(litepcie_device, iterations, packet_size);

    return 0;
}