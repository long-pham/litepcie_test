# LitePCIe Kernel Module IOCTL Interface Guide

This guide provides comprehensive documentation for the LitePCIe kernel module's IOCTL interface, including detailed examples for register access, DMA operations, and other features.

## Table of Contents
- [Overview](#overview)
- [IOCTL Interface](#ioctl-interface)
- [Register Access](#register-access)
- [DMA Operations](#dma-operations)
- [Memory Mapping](#memory-mapping)
- [Flash Operations](#flash-operations)
- [ICAP Operations](#icap-operations)
- [Latency Testing](#latency-testing)
- [Lock Management](#lock-management)
- [Programming Examples](#programming-examples)
- [Error Handling](#error-handling)

## Overview

The LitePCIe kernel module provides a character device interface (`/dev/litepcie*`) for userspace applications to interact with LitePCIe FPGA devices. The primary interface uses IOCTL commands for various operations including:

- Direct register read/write access
- DMA buffer management and control
- Memory-mapped DMA buffer access
- Flash memory operations
- ICAP (Internal Configuration Access Port) control
- Hardware latency measurements
- Multi-process synchronization

## IOCTL Interface

All IOCTL commands use the magic number `'S'` (0x53) and are defined in `litepcie.h`:

```c
#define LITEPCIE_IOCTL 'S'
```

### Available IOCTL Commands

| Command | Number | Direction | Description |
|---------|--------|-----------|-------------|
| `LITEPCIE_IOCTL_REG` | 0 | `_IOWR` | Register read/write |
| `LITEPCIE_IOCTL_FLASH` | 1 | `_IOWR` | Flash SPI operations |
| `LITEPCIE_IOCTL_ICAP` | 2 | `_IOWR` | ICAP write operations |
| `LITEPCIE_IOCTL_DMA` | 20 | `_IOW` | DMA configuration |
| `LITEPCIE_IOCTL_DMA_WRITER` | 21 | `_IOWR` | DMA writer control |
| `LITEPCIE_IOCTL_DMA_READER` | 22 | `_IOWR` | DMA reader control |
| `LITEPCIE_IOCTL_MMAP_DMA_INFO` | 24 | `_IOR` | Get DMA buffer info |
| `LITEPCIE_IOCTL_LOCK` | 25 | `_IOWR` | Lock management |
| `LITEPCIE_IOCTL_MMAP_DMA_WRITER_UPDATE` | 26 | `_IOW` | Update writer count |
| `LITEPCIE_IOCTL_MMAP_DMA_READER_UPDATE` | 27 | `_IOW` | Update reader count |
| `LITEPCIE_IOCTL_LATENCY_TEST` | 30 | `_IOWR` | Latency measurement |

## Register Access

### Structure Definition
```c
struct litepcie_ioctl_reg {
    uint32_t addr;      // Register address
    uint32_t val;       // Value to write or value read
    uint8_t is_write;   // 0 = read, 1 = write
};
```

### Register Read Example
```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "litepcie.h"

int read_register(int fd, uint32_t addr, uint32_t *value) {
    struct litepcie_ioctl_reg reg;
    
    reg.addr = addr;
    reg.is_write = 0;  // Read operation
    
    if (ioctl(fd, LITEPCIE_IOCTL_REG, &reg) < 0) {
        perror("ioctl read");
        return -1;
    }
    
    *value = reg.val;
    return 0;
}

// Usage
int main() {
    int fd = open("/dev/litepcie0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    uint32_t value;
    // Read scratch register at offset 0x4
    if (read_register(fd, 0x4, &value) == 0) {
        printf("Register 0x4 = 0x%08x\n", value);
    }
    
    close(fd);
    return 0;
}
```

### Register Write Example
```c
int write_register(int fd, uint32_t addr, uint32_t value) {
    struct litepcie_ioctl_reg reg;
    
    reg.addr = addr;
    reg.val = value;
    reg.is_write = 1;  // Write operation
    
    if (ioctl(fd, LITEPCIE_IOCTL_REG, &reg) < 0) {
        perror("ioctl write");
        return -1;
    }
    
    return 0;
}

// Write 0xDEADBEEF to scratch register
write_register(fd, 0x4, 0xDEADBEEF);
```

## DMA Operations

### DMA Configuration
```c
struct litepcie_ioctl_dma {
    uint8_t loopback_enable;  // 0 = normal, 1 = loopback mode
};

// Enable loopback mode
struct litepcie_ioctl_dma dma_config;
dma_config.loopback_enable = 1;
ioctl(fd, LITEPCIE_IOCTL_DMA, &dma_config);
```

### DMA Writer Control
```c
struct litepcie_ioctl_dma_writer {
    uint8_t enable;       // 0 = disable, 1 = enable
    int64_t hw_count;     // Hardware buffer count (returned)
    int64_t sw_count;     // Software buffer count (returned)
};

// Enable DMA writer
struct litepcie_ioctl_dma_writer writer;
writer.enable = 1;
if (ioctl(fd, LITEPCIE_IOCTL_DMA_WRITER, &writer) == 0) {
    printf("Writer enabled. HW count: %lld, SW count: %lld\n", 
           writer.hw_count, writer.sw_count);
}
```

### DMA Reader Control
```c
struct litepcie_ioctl_dma_reader {
    uint8_t enable;       // 0 = disable, 1 = enable
    int64_t hw_count;     // Hardware buffer count (returned)
    int64_t sw_count;     // Software buffer count (returned)
};

// Enable DMA reader
struct litepcie_ioctl_dma_reader reader;
reader.enable = 1;
if (ioctl(fd, LITEPCIE_IOCTL_DMA_READER, &reader) == 0) {
    printf("Reader enabled. HW count: %lld, SW count: %lld\n", 
           reader.hw_count, reader.sw_count);
}
```

### Complete DMA Transfer Example
```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include "litepcie.h"

#define BUFFER_SIZE 8192

int perform_dma_transfer(int fd) {
    struct litepcie_ioctl_dma_writer writer;
    struct litepcie_ioctl_dma_reader reader;
    uint8_t *tx_buffer, *rx_buffer;
    int ret = 0;
    
    // Allocate buffers
    tx_buffer = malloc(BUFFER_SIZE);
    rx_buffer = malloc(BUFFER_SIZE);
    
    // Fill TX buffer with test pattern
    for (int i = 0; i < BUFFER_SIZE; i++) {
        tx_buffer[i] = i & 0xFF;
    }
    
    // Enable DMA reader first
    reader.enable = 1;
    if (ioctl(fd, LITEPCIE_IOCTL_DMA_READER, &reader) < 0) {
        perror("Enable reader");
        ret = -1;
        goto cleanup;
    }
    
    // Enable DMA writer
    writer.enable = 1;
    if (ioctl(fd, LITEPCIE_IOCTL_DMA_WRITER, &writer) < 0) {
        perror("Enable writer");
        ret = -1;
        goto cleanup;
    }
    
    // Write data
    if (write(fd, tx_buffer, BUFFER_SIZE) != BUFFER_SIZE) {
        perror("write");
        ret = -1;
        goto cleanup;
    }
    
    // Read data back
    if (read(fd, rx_buffer, BUFFER_SIZE) != BUFFER_SIZE) {
        perror("read");
        ret = -1;
        goto cleanup;
    }
    
    // Verify data
    if (memcmp(tx_buffer, rx_buffer, BUFFER_SIZE) == 0) {
        printf("DMA transfer successful!\n");
    } else {
        printf("DMA transfer failed - data mismatch\n");
        ret = -1;
    }
    
    // Disable DMA
    writer.enable = 0;
    ioctl(fd, LITEPCIE_IOCTL_DMA_WRITER, &writer);
    reader.enable = 0;
    ioctl(fd, LITEPCIE_IOCTL_DMA_READER, &reader);
    
cleanup:
    free(tx_buffer);
    free(rx_buffer);
    return ret;
}
```

## Memory Mapping

### Get DMA Buffer Information
```c
struct litepcie_ioctl_mmap_dma_info {
    uint64_t dma_tx_buf_offset;   // TX buffer offset for mmap
    uint64_t dma_tx_buf_size;     // Size of each TX buffer
    uint64_t dma_tx_buf_count;    // Number of TX buffers
    uint64_t dma_rx_buf_offset;   // RX buffer offset for mmap
    uint64_t dma_rx_buf_size;     // Size of each RX buffer
    uint64_t dma_rx_buf_count;    // Number of RX buffers
};

struct litepcie_ioctl_mmap_dma_info info;
if (ioctl(fd, LITEPCIE_IOCTL_MMAP_DMA_INFO, &info) == 0) {
    printf("TX buffers: %llu x %llu bytes at offset 0x%llx\n",
           info.dma_tx_buf_count, info.dma_tx_buf_size, 
           info.dma_tx_buf_offset);
    printf("RX buffers: %llu x %llu bytes at offset 0x%llx\n",
           info.dma_rx_buf_count, info.dma_rx_buf_size, 
           info.dma_rx_buf_offset);
}
```

### Memory-Mapped DMA Example
```c
#include <sys/mman.h>

int use_mmap_dma(int fd) {
    struct litepcie_ioctl_mmap_dma_info info;
    void *dma_map;
    uint8_t *tx_buffers, *rx_buffers;
    size_t map_size;
    
    // Get DMA buffer info
    if (ioctl(fd, LITEPCIE_IOCTL_MMAP_DMA_INFO, &info) < 0) {
        perror("Get DMA info");
        return -1;
    }
    
    // Calculate total map size
    map_size = info.dma_rx_buf_offset + 
               (info.dma_rx_buf_size * info.dma_rx_buf_count);
    
    // Memory map the DMA buffers
    dma_map = mmap(NULL, map_size, PROT_READ | PROT_WRITE, 
                   MAP_SHARED, fd, 0);
    if (dma_map == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    
    // Get buffer pointers
    tx_buffers = (uint8_t *)dma_map + info.dma_tx_buf_offset;
    rx_buffers = (uint8_t *)dma_map + info.dma_rx_buf_offset;
    
    // Access buffers directly
    // TX buffer 0
    memset(tx_buffers, 0xAA, info.dma_tx_buf_size);
    
    // RX buffer 0
    printf("First byte of RX buffer 0: 0x%02x\n", rx_buffers[0]);
    
    // Update software counters after processing
    struct litepcie_ioctl_mmap_dma_update update;
    update.sw_count = 1;  // Processed 1 buffer
    ioctl(fd, LITEPCIE_IOCTL_MMAP_DMA_WRITER_UPDATE, &update);
    ioctl(fd, LITEPCIE_IOCTL_MMAP_DMA_READER_UPDATE, &update);
    
    // Cleanup
    munmap(dma_map, map_size);
    return 0;
}
```

## Flash Operations

### Flash SPI Access
```c
struct litepcie_ioctl_flash {
    int tx_len;        // 8 to 40 bits to transmit
    __u64 tx_data;     // Data to transmit (8 to 40 bits)
    __u64 rx_data;     // Data received (40 bits)
};

// Example: Read flash ID
struct litepcie_ioctl_flash flash;
flash.tx_len = 8;
flash.tx_data = 0x9F;  // Read ID command
if (ioctl(fd, LITEPCIE_IOCTL_FLASH, &flash) == 0) {
    printf("Flash ID: 0x%llx\n", flash.rx_data);
}
```

## ICAP Operations

### ICAP Write Access
```c
struct litepcie_ioctl_icap {
    uint8_t addr;      // ICAP address
    uint32_t data;     // Data to write
};

// Write to ICAP
struct litepcie_ioctl_icap icap;
icap.addr = 0x00;
icap.data = 0x12345678;
ioctl(fd, LITEPCIE_IOCTL_ICAP, &icap);
```

## Latency Testing

### Kernel Latency Measurement
```c
struct litepcie_ioctl_latency {
    uint32_t iterations;   // Number of measurements to perform
    uint64_t min_ns;      // Minimum latency (output)
    uint64_t max_ns;      // Maximum latency (output)
    uint64_t avg_ns;      // Average latency (output)
    uint64_t total_ns;    // Total time (output)
};

// Perform latency test
struct litepcie_ioctl_latency lat;
lat.iterations = 1000;
if (ioctl(fd, LITEPCIE_IOCTL_LATENCY_TEST, &lat) == 0) {
    printf("Latency test results (%u iterations):\n", lat.iterations);
    printf("  Min: %llu ns\n", lat.min_ns);
    printf("  Max: %llu ns\n", lat.max_ns);
    printf("  Avg: %llu ns\n", lat.avg_ns);
    printf("  Total: %llu ns\n", lat.total_ns);
}
```

## Lock Management

### Multi-Process Synchronization
```c
struct litepcie_ioctl_lock {
    uint8_t dma_reader_request;   // Request reader lock
    uint8_t dma_writer_request;   // Request writer lock
    uint8_t dma_reader_release;   // Release reader lock
    uint8_t dma_writer_release;   // Release writer lock
    uint8_t dma_reader_status;    // Reader lock status (output)
    uint8_t dma_writer_status;    // Writer lock status (output)
};

// Request exclusive access to DMA writer
struct litepcie_ioctl_lock lock = {0};
lock.dma_writer_request = 1;
if (ioctl(fd, LITEPCIE_IOCTL_LOCK, &lock) == 0) {
    if (lock.dma_writer_status) {
        printf("Got writer lock\n");
        // Do exclusive writer operations
        
        // Release lock when done
        memset(&lock, 0, sizeof(lock));
        lock.dma_writer_release = 1;
        ioctl(fd, LITEPCIE_IOCTL_LOCK, &lock);
    } else {
        printf("Writer lock busy\n");
    }
}
```

## Programming Examples

### Complete Application Example
```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "litepcie.h"

#define DEVICE_PATH "/dev/litepcie0"

// Helper function to perform register read/write
int reg_access(int fd, uint32_t addr, uint32_t *value, int is_write) {
    struct litepcie_ioctl_reg reg;
    
    reg.addr = addr;
    reg.is_write = is_write;
    if (is_write) {
        reg.val = *value;
    }
    
    if (ioctl(fd, LITEPCIE_IOCTL_REG, &reg) < 0) {
        return -errno;
    }
    
    if (!is_write) {
        *value = reg.val;
    }
    
    return 0;
}

// Helper function to check device identification
int check_device_id(int fd) {
    uint32_t id;
    
    // Read device ID register (usually at offset 0)
    if (reg_access(fd, 0x0, &id, 0) < 0) {
        perror("Read device ID");
        return -1;
    }
    
    printf("Device ID: 0x%08x\n", id);
    
    // Check if this is a valid LitePCIe device
    if ((id & 0xFFFF0000) != 0x12340000) {
        fprintf(stderr, "Invalid device ID\n");
        return -1;
    }
    
    return 0;
}

// Main application
int main() {
    int fd;
    int ret = 0;
    
    // Open device
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    printf("LitePCIe device opened successfully\n");
    
    // Check device ID
    if (check_device_id(fd) < 0) {
        ret = 1;
        goto cleanup;
    }
    
    // Perform scratch register test
    uint32_t test_val = 0xDEADBEEF;
    uint32_t read_val;
    
    printf("\nScratch register test:\n");
    printf("Writing 0x%08x to scratch register\n", test_val);
    
    if (reg_access(fd, 0x4, &test_val, 1) < 0) {
        perror("Write scratch");
        ret = 1;
        goto cleanup;
    }
    
    if (reg_access(fd, 0x4, &read_val, 0) < 0) {
        perror("Read scratch");
        ret = 1;
        goto cleanup;
    }
    
    if (read_val == test_val) {
        printf("Scratch register test PASSED (read: 0x%08x)\n", read_val);
    } else {
        printf("Scratch register test FAILED (expected: 0x%08x, got: 0x%08x)\n",
               test_val, read_val);
        ret = 1;
    }
    
    // Perform latency test
    printf("\nPerforming latency test...\n");
    struct litepcie_ioctl_latency lat;
    lat.iterations = 1000;
    
    if (ioctl(fd, LITEPCIE_IOCTL_LATENCY_TEST, &lat) == 0) {
        printf("Round-trip latency (1000 iterations):\n");
        printf("  Min: %.3f µs\n", lat.min_ns / 1000.0);
        printf("  Max: %.3f µs\n", lat.max_ns / 1000.0);
        printf("  Avg: %.3f µs\n", lat.avg_ns / 1000.0);
    } else {
        printf("Latency test not supported by kernel module\n");
    }
    
cleanup:
    close(fd);
    return ret;
}
```

### Makefile for User Applications
```makefile
CC = gcc
CFLAGS = -Wall -O2 -I../../kernel
LDFLAGS = 

TARGETS = litepcie_example

all: $(TARGETS)

litepcie_example: litepcie_example.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)

.PHONY: all clean
```

## Error Handling

### Common Error Codes

| Error | Description | Solution |
|-------|-------------|----------|
| `ENODEV` | Device not found | Check if module is loaded and device exists |
| `EACCES` | Permission denied | Run as root or adjust device permissions |
| `EBUSY` | Device busy | Another process may have exclusive access |
| `EINVAL` | Invalid argument | Check IOCTL parameters |
| `ENOMEM` | Out of memory | System may be low on memory |
| `EIO` | I/O error | Check dmesg for hardware errors |

### Error Handling Example
```c
#include <errno.h>

void handle_ioctl_error(const char *operation) {
    switch(errno) {
        case ENODEV:
            fprintf(stderr, "%s: Device not found\n", operation);
            break;
        case EACCES:
            fprintf(stderr, "%s: Permission denied (try sudo)\n", operation);
            break;
        case EBUSY:
            fprintf(stderr, "%s: Device busy\n", operation);
            break;
        case EINVAL:
            fprintf(stderr, "%s: Invalid argument\n", operation);
            break;
        default:
            perror(operation);
    }
}

// Usage
if (ioctl(fd, LITEPCIE_IOCTL_REG, &reg) < 0) {
    handle_ioctl_error("Register access");
    return -1;
}
```

## Performance Considerations

1. **Buffer Alignment**: DMA buffers are page-aligned for optimal performance
2. **Batch Operations**: Process multiple buffers before updating counters
3. **CPU Affinity**: Pin threads to specific CPUs for consistent performance
4. **Polling vs Interrupts**: Use interrupts for low CPU usage, polling for low latency

## Security Notes

- The device files require appropriate permissions (usually root)
- DMA operations can access system memory - use with caution
- Lock mechanisms prevent conflicting access between processes
- Always validate data from untrusted sources before DMA operations

## Debugging

### Enable Debug Output
```bash
# Enable dynamic debug (if supported)
echo 'module litepcie +p' | sudo tee /sys/kernel/debug/dynamic_debug/control

# Check kernel messages
dmesg -w | grep litepcie
```

### Check Device State
```bash
# View device info
cat /sys/class/misc/litepcie*/dev

# Check interrupts
cat /proc/interrupts | grep litepcie
```

## Conclusion

The LitePCIe kernel module provides a comprehensive IOCTL interface for controlling PCIe FPGA devices. This guide covers the main features, but always refer to the latest kernel module source code for the most up-to-date interface definitions and capabilities.