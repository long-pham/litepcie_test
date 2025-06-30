/*
 * LitePCIe Kernel-Assisted Latency Test
 * 
 * Uses the kernel latency IOCTL for precise measurements.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "kernel/litepcie.h"
#include "kernel/litepcie_latency.h"

/* Test kernel latency support */
static int test_kernel_latency(int fd, int iterations) {
    struct litepcie_ioctl_latency lat;
    int ret;
    
    lat.iterations = iterations;
    
    ret = ioctl(fd, LITEPCIE_IOCTL_LATENCY_TEST, &lat);
    if (ret < 0) {
        if (errno == ENOTTY || errno == EINVAL) {
            printf("Kernel latency test not supported (IOCTL not found)\n");
            printf("Make sure you loaded the updated kernel module\n");
            return -1;
        }
        perror("ioctl LATENCY_TEST");
        return -1;
    }
    
    printf("\nKernel Latency Test Results:\n");
    printf("  Iterations: %u\n", lat.iterations);
    printf("  Min latency: %.3f µs (%.1f ns)\n", lat.min_ns / 1000.0, (double)lat.min_ns);
    printf("  Avg latency: %.3f µs (%.1f ns)\n", lat.avg_ns / 1000.0, (double)lat.avg_ns);
    printf("  Max latency: %.3f µs (%.1f ns)\n", lat.max_ns / 1000.0, (double)lat.max_ns);
    printf("  Total time:  %.3f ms\n", lat.total_ns / 1000000.0);
    
    return 0;
}

int main(int argc, char *argv[]) {
    const char *device = "/dev/litepcie0";
    int iterations = 10000;
    int fd;
    
    if (argc > 1)
        device = argv[1];
    if (argc > 2)
        iterations = atoi(argv[2]);
    
    /* Open device */
    fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("open");
        fprintf(stderr, "Failed to open %s\n", device);
        return 1;
    }
    
    printf("LitePCIe Kernel Latency Test\n");
    printf("Device: %s\n", device);
    printf("Performing %d iterations in kernel space...\n", iterations);
    
    /* Try kernel latency test */
    if (test_kernel_latency(fd, iterations) < 0) {
        printf("\nKernel latency test failed.\n");
        printf("You can also use ./litepcie_latency_test for userspace measurements.\n");
    }
    
    close(fd);
    return 0;
}