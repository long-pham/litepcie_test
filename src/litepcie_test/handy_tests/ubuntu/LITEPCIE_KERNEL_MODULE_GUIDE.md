# LitePCIe Kernel Module Guide

This guide provides comprehensive documentation for the LitePCIe kernel module, covering all features and usage examples.

## Table of Contents
- [Overview](#overview)
- [Module Loading](#module-loading)
- [Device Files](#device-files)
- [Register Access](#register-access)
- [DMA Operations](#dma-operations)
- [Memory Mapping](#memory-mapping)
- [Interrupt Handling](#interrupt-handling)
- [IOCTL Commands](#ioctl-commands)
- [Programming Examples](#programming-examples)
- [Debugging](#debugging)

## Overview

The LitePCIe kernel module provides a Linux driver interface for LiteX-based PCIe designs. It supports:
- Memory-mapped register access
- High-performance DMA transfers
- Interrupt handling
- User-space memory mapping

### Code Style Note
Throughout this guide, we use C99 designated initializers for struct initialization:
```c
// Preferred style - clear and concise
struct example_struct s = {
    .field1 = value1,
    .field2 = value2
};

// Instead of the older style
struct example_struct s;
s.field1 = value1;
s.field2 = value2;
```

### Architecture

```
User Space                    Kernel Space                   Hardware
┌──────────┐                 ┌──────────────┐              ┌─────────┐
│   App    │ ◄──────────────►│  LitePCIe    │◄────────────►│  FPGA   │
│          │  ioctl/mmap     │  Driver      │   PCIe       │         │
└──────────┘                 └──────────────┘              └─────────┘
     │                              │                            │
     │                              │                            │
     ▼                              ▼                            ▼
/dev/litepcie0               Kernel Module               PCIe Endpoint
```

## Module Loading

### Basic Loading
```bash
# Load the module
sudo modprobe litepcie

# Or manually with insmod
sudo insmod litepcie.ko

# Load with parameters
sudo modprobe litepcie dma_buffering=1 debug=1
```

### Module Parameters
```bash
# View available parameters
modinfo litepcie

# Common parameters:
# - dma_buffering: Enable DMA buffering (0/1)
# - debug: Enable debug messages (0/1)
# - max_dma_len: Maximum DMA transfer length
```

### Verification
```bash
# Check if module is loaded
lsmod | grep litepcie

# View kernel messages
dmesg | grep litepcie

# Check PCIe device
lspci -v | grep -A 10 "LiteX"
```

## Device Files

The driver creates device files for each PCIe card:

```bash
/dev/litepcie0  # First card
/dev/litepcie1  # Second card (if present)
```

### Permissions
```bash
# Set permissions for user access
sudo chmod 666 /dev/litepcie0

# Or create udev rule
echo 'KERNEL=="litepcie*", MODE="0666"' | sudo tee /etc/udev/rules.d/99-litepcie.rules
```

## Register Access

### IOCTL Interface

```c
#include "litepcie.h"

// Register read
uint32_t reg_read(int fd, uint32_t addr) {
    struct litepcie_ioctl_reg reg = {
        .addr = addr,
        .is_write = 0
    };
    
    if (ioctl(fd, LITEPCIE_IOCTL_REG, &reg) < 0) {
        perror("ioctl read");
        return 0;
    }
    
    return reg.val;
}

// Register write
void reg_write(int fd, uint32_t addr, uint32_t val) {
    struct litepcie_ioctl_reg reg = {
        .addr = addr,
        .val = val,
        .is_write = 1
    };
    
    if (ioctl(fd, LITEPCIE_IOCTL_REG, &reg) < 0) {
        perror("ioctl write");
    }
}
```

### Memory-Mapped Access

Memory-mapped I/O (MMIO) allows user-space applications to directly access FPGA registers as if they were regular memory. Here's how it works:

#### How PCIe Memory Mapping Works

1. **PCIe Address Space**: The FPGA exposes BAR (Base Address Register) regions that are mapped into the host's physical address space
2. **Kernel Mapping**: The LitePCIe driver maps these physical addresses into kernel virtual address space
3. **User-Space Mapping**: Using `mmap()`, applications can map these regions into user virtual address space
4. **Hardware Translation**: CPU memory operations are automatically translated to PCIe transactions

```
User Space Write:  *(volatile uint32_t*)addr = 0x1234;
      ↓
CPU Write Instruction
      ↓
MMU Translation (Virtual → Physical)
      ↓
PCIe Root Complex
      ↓
PCIe TLP (Transaction Layer Packet) Write
      ↓
FPGA PCIe Endpoint
      ↓
CSR Register Update
```

#### Important Concepts

1. **Automatic PCIe Transfer**: Yes, writes to mapped memory automatically generate PCIe write transactions
2. **No CPU Intervention**: After mapping, no system calls needed - direct hardware access
3. **Cache Coherency**: Use `volatile` to prevent compiler optimizations and ensure writes reach hardware
4. **Write Posting**: PCIe writes are typically posted (fire-and-forget), reads are non-posted (wait for completion)

#### Example with Detailed Explanation

```c
// Map CSR region
void* csr_base = mmap(NULL, CSR_SIZE, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
if (csr_base == MAP_FAILED) {
    perror("mmap");
    return -1;
}

// IMPORTANT: Use volatile to ensure memory operations aren't optimized away
volatile uint32_t* regs = (uint32_t*)csr_base;

// READ: This triggers a PCIe read transaction
// 1. CPU issues load instruction
// 2. Address is translated to PCIe address
// 3. PCIe read request sent to FPGA
// 4. FPGA responds with data
// 5. Data returned to CPU
uint32_t value = regs[CSR_IDENTIFIER_MEM_BASE/4];

// WRITE: This triggers a PCIe write transaction  
// 1. CPU issues store instruction
// 2. Address is translated to PCIe address
// 3. PCIe write packet sent to FPGA
// 4. FPGA updates the CSR register
// 5. Write typically completes without waiting for acknowledgment
regs[CSR_LEDS_OUT_ADDR/4] = 0xFF;

// Memory barrier to ensure write completes
__sync_synchronize();  // or asm volatile("" ::: "memory");

// Unmap when done
munmap(csr_base, CSR_SIZE);
```

#### Performance Characteristics

```c
// Latency comparison example
void benchmark_access_methods(int fd) {
    struct timespec start, end;
    const int iterations = 10000;
    
    // Method 1: IOCTL (higher overhead)
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        reg_read(fd, CSR_TIMER0_VALUE_ADDR);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("IOCTL: %ld ns per operation\n", 
           (end.tv_nsec - start.tv_nsec) / iterations);
    
    // Method 2: MMAP (lower overhead)
    volatile uint32_t* regs = mmap(...);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        volatile uint32_t val = regs[CSR_TIMER0_VALUE_ADDR/4];
        (void)val; // Prevent optimization
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("MMAP: %ld ns per operation\n",
           (end.tv_nsec - start.tv_nsec) / iterations);
}
```

#### Memory Barriers and Ordering

```c
// Ensure write ordering
volatile uint32_t* regs = (uint32_t*)csr_base;

// Multiple writes - order not guaranteed without barriers
regs[ADDR1/4] = VALUE1;
regs[ADDR2/4] = VALUE2;  // Might complete before VALUE1!

// Force ordering with memory barrier
regs[ADDR1/4] = VALUE1;
__sync_synchronize();    // Ensure VALUE1 write completes
regs[ADDR2/4] = VALUE2;  // Now guaranteed to happen after VALUE1

// Read-after-write hazard
regs[CONFIG_REG/4] = NEW_CONFIG;
__sync_synchronize();
uint32_t status = regs[STATUS_REG/4];  // Guaranteed to see new config
```

#### Advantages of Memory-Mapped Access

1. **Low Latency**: No system call overhead
2. **High Throughput**: Can issue back-to-back operations
3. **Simple Code**: Looks like regular memory access
4. **Atomic Operations**: Some architectures support atomic PCIe operations

#### Limitations and Considerations

1. **Page Size**: Minimum mapping is typically 4KB (page size)
2. **Alignment**: Addresses must be naturally aligned
3. **Error Handling**: No automatic error detection (unlike IOCTL)
4. **Security**: Requires appropriate permissions
5. **Caching**: Must use volatile and barriers appropriately

### Common CSR Addresses

```c
// From csr.h
#define CSR_IDENTIFIER_MEM_BASE    0x0000
#define CSR_UART_BASE              0x1000
#define CSR_TIMER0_BASE            0x2000
#define CSR_PCIE_DMA0_BASE         0x3000
#define CSR_FLASH_BASE             0x4000

// DMA registers
#define PCIE_DMA_WRITER_ENABLE_OFFSET        0x00
#define PCIE_DMA_WRITER_TABLE_FLUSH_OFFSET   0x04
#define PCIE_DMA_WRITER_TABLE_LOOP_PROG_OFFSET 0x08
#define PCIE_DMA_WRITER_TABLE_LOOP_STATUS_OFFSET 0x0C
#define PCIE_DMA_WRITER_TABLE_VALUE_OFFSET   0x10
#define PCIE_DMA_WRITER_TABLE_WE_OFFSET      0x14
```

## DMA Operations

### DMA Modes

1. **Table Mode**: Uses descriptor tables for scatter-gather DMA
2. **Buffering Mode**: Uses kernel buffers for data transfers
3. **Zero-Copy Mode**: Direct user-space buffer access

### Basic DMA Transfer

```c
#include "litepcie_dma.h"

// Initialize DMA with struct initialization
struct litepcie_dma_ctrl dma_ctrl = {
    .loopback = 0,      // External loopback
    .use_reader = 1,
    .use_writer = 1,
    .reader_enable = 0,  // Will enable after init
    .writer_enable = 0   // Will enable after init
};

if (litepcie_dma_init(&dma_ctrl, "/dev/litepcie0", 0)) {
    fprintf(stderr, "DMA init failed\n");
    return -1;
}

// Enable DMA after initialization
dma_ctrl.reader_enable = 1;
dma_ctrl.writer_enable = 1;

// Write data
char* write_buf = litepcie_dma_next_write_buffer(&dma_ctrl);
if (write_buf) {
    memcpy(write_buf, data, DMA_BUFFER_SIZE);
}

// Process DMA
litepcie_dma_process(&dma_ctrl);

// Read data
char* read_buf = litepcie_dma_next_read_buffer(&dma_ctrl);
if (read_buf) {
    memcpy(data, read_buf, DMA_BUFFER_SIZE);
}

// Cleanup
litepcie_dma_cleanup(&dma_ctrl);
```

### DMA Descriptor Format

```c
// DMA descriptor (64-bit)
struct dma_descriptor {
    uint32_t length  : 24;  // Transfer length
    uint32_t irq_dis : 1;   // Disable IRQ
    uint32_t last_dis: 1;   // Not last descriptor
    uint32_t reserved: 6;
    uint32_t address;       // Target address
};

// Example: Setup DMA descriptor
uint64_t descriptor = DMA_LAST_DISABLE | DMA_IRQ_DISABLE | length;
reg_write(fd, CSR_PCIE_DMA0_BASE + PCIE_DMA_WRITER_TABLE_VALUE_OFFSET, 
          descriptor & 0xFFFFFFFF);
reg_write(fd, CSR_PCIE_DMA0_BASE + PCIE_DMA_WRITER_TABLE_VALUE_OFFSET + 4,
          target_addr);
```

### DMA to FPGA Memory Regions

```c
// Common memory regions (from mem.h)
#define MAIN_RAM_BASE     0x40000000
#define MAIN_RAM_SIZE     0x10000000  // 256MB

#define SRAM_BASE         0x10000000
#define SRAM_SIZE         0x00001000  // 4KB

// DMA to MAIN_RAM
uint32_t target = MAIN_RAM_BASE + offset;
setup_dma_transfer(fd, buffer, length, target);

// DMA to SRAM
uint32_t sram_addr = SRAM_BASE;
setup_dma_transfer(fd, buffer, length, sram_addr);
```

## Memory Mapping

### Mapping FPGA Memory

```c
// Map MAIN_RAM for direct access
size_t map_size = 1024 * 1024;  // 1MB
void* fpga_mem = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, MAIN_RAM_BASE);

if (fpga_mem == MAP_FAILED) {
    perror("mmap MAIN_RAM");
    return -1;
}

// Direct memory access
uint32_t* mem = (uint32_t*)fpga_mem;
mem[0] = 0xDEADBEEF;
uint32_t value = mem[1];

// Unmap
munmap(fpga_mem, map_size);
```

### DMA Buffer Mapping

```c
// Get DMA buffer info
struct litepcie_ioctl_dma_info info = {0};  // Initialize to zero
if (ioctl(fd, LITEPCIE_IOCTL_DMA_INFO, &info) < 0) {
    perror("ioctl dma_info");
    return -1;
}

// Map DMA buffers
void* dma_buf = mmap(NULL, info.buffer_size * info.buffer_count,
                     PROT_READ | PROT_WRITE, MAP_SHARED,
                     fd, info.buffer_offset);
```

## Interrupt Handling

### Enable Interrupts

```c
// Enable MSI interrupts
struct litepcie_ioctl_irq irq_cfg = {
    .enable = 1,
    .vector = 0
};

if (ioctl(fd, LITEPCIE_IOCTL_IRQ, &irq_cfg) < 0) {
    perror("ioctl irq");
}
```

### Wait for Interrupt

```c
// Poll for interrupt
struct pollfd pfd;
pfd.fd = fd;
pfd.events = POLLIN;

int ret = poll(&pfd, 1, 1000);  // 1 second timeout
if (ret > 0 && (pfd.revents & POLLIN)) {
    // Interrupt occurred
    uint32_t status = reg_read(fd, CSR_INTERRUPT_STATUS);
    // Handle interrupt...
}
```

## IOCTL Commands

### Available IOCTLs

```c
// Register access
#define LITEPCIE_IOCTL_REG        _IOWR('P', 0, struct litepcie_ioctl_reg)

// DMA control
#define LITEPCIE_IOCTL_DMA        _IOW('P', 1, struct litepcie_ioctl_dma)
#define LITEPCIE_IOCTL_DMA_INFO   _IOR('P', 2, struct litepcie_ioctl_dma_info)

// Interrupt control
#define LITEPCIE_IOCTL_IRQ        _IOW('P', 3, struct litepcie_ioctl_irq)

// Flash access
#define LITEPCIE_IOCTL_FLASH      _IOWR('P', 4, struct litepcie_ioctl_flash)

// MMAP info
#define LITEPCIE_IOCTL_MMAP_INFO  _IOR('P', 5, struct litepcie_ioctl_mmap_info)
```

### IOCTL Structures

```c
// Register access
struct litepcie_ioctl_reg {
    uint32_t addr;
    uint32_t val;
    uint8_t is_write;
};

// DMA configuration
struct litepcie_ioctl_dma {
    uint8_t loopback_enable;
    uint8_t external_loopback;
    uint8_t buffering_enable;
    uint32_t buffer_size;
    uint32_t buffer_count;
};

// DMA info
struct litepcie_ioctl_dma_info {
    uint32_t buffer_size;
    uint32_t buffer_count;
    uint64_t buffer_offset;
    uint8_t buffering_enabled;
};
```

## Programming Examples

### Example 1: LED Control

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "litepcie.h"

int main() {
    int fd = open("/dev/litepcie0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    // Blink LEDs
    for (int i = 0; i < 10; i++) {
        reg_write(fd, CSR_LEDS_OUT_ADDR, 0xFF);
        usleep(500000);
        reg_write(fd, CSR_LEDS_OUT_ADDR, 0x00);
        usleep(500000);
    }
    
    close(fd);
    return 0;
}
```

### Example 2: Memory Test

```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "litepcie.h"
#include "mem.h"

int main() {
    int fd = open("/dev/litepcie0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    // Map MAIN_RAM
    size_t size = 1024 * 1024;  // 1MB
    void* mem = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, MAIN_RAM_BASE);
    
    if (mem == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }
    
    // Write test pattern
    uint32_t* data = (uint32_t*)mem;
    for (int i = 0; i < size/4; i++) {
        data[i] = i;
    }
    
    // Verify
    int errors = 0;
    for (int i = 0; i < size/4; i++) {
        if (data[i] != i) {
            errors++;
        }
    }
    
    printf("Memory test: %d errors\n", errors);
    
    munmap(mem, size);
    close(fd);
    return 0;
}
```

### Example 3: DMA Benchmark

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "litepcie_dma.h"

int main() {
    // Initialize DMA with all settings at once
    struct litepcie_dma_ctrl dma_ctrl = {
        .loopback = 1,       // Internal loopback for testing
        .use_reader = 1,
        .use_writer = 1,
        .reader_enable = 0,  // Enable after init
        .writer_enable = 0   // Enable after init
    };
    
    if (litepcie_dma_init(&dma_ctrl, "/dev/litepcie0", 0)) {
        fprintf(stderr, "DMA init failed\n");
        return 1;
    }
    
    // Enable DMA transfers
    dma_ctrl.reader_enable = 1;
    dma_ctrl.writer_enable = 1;
    
    // Benchmark
    size_t total_bytes = 0;
    time_t start = time(NULL);
    
    while (time(NULL) - start < 10) {  // 10 seconds
        char* buf = litepcie_dma_next_write_buffer(&dma_ctrl);
        if (buf) {
            // Fill buffer with data
            memset(buf, 0xAA, DMA_BUFFER_SIZE);
            total_bytes += DMA_BUFFER_SIZE;
        }
        
        litepcie_dma_process(&dma_ctrl);
    }
    
    double throughput = total_bytes / 10.0 / 1024 / 1024;
    printf("Throughput: %.2f MB/s\n", throughput);
    
    litepcie_dma_cleanup(&dma_ctrl);
    return 0;
}
```

### Example 4: Interrupt-Driven DMA

```c
#include <stdio.h>
#include <poll.h>
#include "litepcie.h"

int main() {
    int fd = open("/dev/litepcie0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    // Enable interrupts
    struct litepcie_ioctl_irq irq_cfg = {
        .enable = 1,
        .vector = 0
    };
    ioctl(fd, LITEPCIE_IOCTL_IRQ, &irq_cfg);
    
    // Setup DMA with IRQ
    setup_dma_with_irq(fd);
    
    // Wait for completion
    struct pollfd pfd = {
        .fd = fd,
        .events = POLLIN
    };
    
    if (poll(&pfd, 1, 5000) > 0) {  // 5 second timeout
        printf("DMA completed\n");
        
        // Clear interrupt
        uint32_t status = reg_read(fd, CSR_INTERRUPT_STATUS);
        reg_write(fd, CSR_INTERRUPT_PENDING, status);
    } else {
        printf("Timeout waiting for DMA\n");
    }
    
    close(fd);
    return 0;
}
```

## Debugging

### Kernel Module Debug

```bash
# Enable debug messages
echo 8 > /proc/sys/kernel/printk

# View kernel log
dmesg -w | grep litepcie

# Module statistics
cat /sys/module/litepcie/parameters/*
```

### Device Information

```bash
# PCIe link status
sudo lspci -vvv -s $(lspci | grep LiteX | cut -d' ' -f1)

# Device resources
cat /proc/iomem | grep litepcie
cat /proc/interrupts | grep litepcie
```

### Common Issues

1. **Permission Denied**
   ```bash
   # Fix permissions
   sudo chmod 666 /dev/litepcie0
   ```

2. **DMA Timeout**
   ```bash
   # Check DMA status
   ./litepcie_util dma_test
   ```

3. **Module Load Failure**
   ```bash
   # Check dependencies
   lsmod | grep litepcie
   dmesg | tail -20
   ```

## Performance Tuning

### PCIe Settings
```bash
# Set max payload size
sudo setpci -s $(lspci | grep LiteX | cut -d' ' -f1) 68.w=5936

# Check link speed
sudo lspci -vvv | grep -A 20 LiteX | grep "LnkSta:"
```

### CPU Settings
```bash
# Disable frequency scaling
sudo cpupower frequency-set -g performance

# Set IRQ affinity
echo 2 > /proc/irq/$(grep litepcie /proc/interrupts | cut -d: -f1)/smp_affinity
```

### Kernel Parameters
```bash
# Edit /etc/default/grub
GRUB_CMDLINE_LINUX="intel_idle.max_cstate=0 processor.max_cstate=0"

# Update grub
sudo update-grub
```

## Conclusion

The LitePCIe kernel module provides a flexible interface for high-performance PCIe communication with FPGA devices. Key features include:

- Low-latency register access via IOCTL or memory mapping
- High-throughput DMA with scatter-gather support
- Interrupt-driven operation for efficient CPU usage
- Zero-copy transfers for maximum performance

For production use, consider:
- Implementing proper error handling
- Using interrupts for DMA completion
- Optimizing buffer sizes for your workload
- Monitoring PCIe link quality and errors