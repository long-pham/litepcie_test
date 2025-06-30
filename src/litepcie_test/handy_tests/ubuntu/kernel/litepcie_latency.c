/* SPDX-License-Identifier: BSD-2-Clause
 *
 * LitePCIe Latency Measurement Implementation
 *
 * This file is part of LitePCIe.
 *
 * Copyright (C) 2024 / Your Name
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ktime.h>
#include <linux/limits.h>
#include <linux/pci.h>
#include <linux/io.h>

#include "litepcie.h"
#include "litepcie_latency.h"
#include "csr.h"

/* Need to match the structure from main.c */
struct litepcie_device {
	struct pci_dev *dev;                          /* PCI device */
	struct platform_device *uart;                 /* UART platform device */
	resource_size_t bar0_size;                    /* Size of BAR0 */
	phys_addr_t bar0_phys_addr;                   /* Physical address of BAR0 */
	uint8_t *bar0_addr;                           /* Virtual address of BAR0 */
	/* Other fields not needed for our purposes */
};

/* Local copies of the register access functions */
static inline uint32_t litepcie_readl(struct litepcie_device *s, uint32_t addr)
{
	uint32_t val;
	val = readl(s->bar0_addr + addr - CSR_BASE);
	return val;
}

static inline void litepcie_writel(struct litepcie_device *s, uint32_t addr, uint32_t val)
{
	writel(val, s->bar0_addr + addr - CSR_BASE);
}

/* Perform latency test in kernel space */
int litepcie_latency_test(struct litepcie_device *dev, struct litepcie_ioctl_latency *lat)
{
	uint64_t start_ns, end_ns, latency_ns;
	uint64_t min_ns = ULLONG_MAX;
	uint64_t max_ns = 0;
	uint64_t total_ns = 0;
	uint32_t i, test_val;
	uint32_t readback;
	unsigned long flags;
	
	/* Validate parameters */
	if (!dev || !lat)
		return -EINVAL;
	
	/* Limit iterations to prevent blocking too long */
	if (lat->iterations > 100000)
		lat->iterations = 100000;
	if (lat->iterations == 0)
		lat->iterations = 1000;
	
	/* Disable interrupts for more accurate measurement */
	local_irq_save(flags);
	
	/* Perform latency measurements */
	for (i = 0; i < lat->iterations; i++) {
		test_val = 0xDEADBEEF ^ i;
		
		/* Measure round-trip time */
		start_ns = ktime_get_ns();
		
		/* Write to scratch register */
		litepcie_writel(dev, CSR_CTRL_SCRATCH_ADDR, test_val);
		
		/* Read back to ensure completion */
		readback = litepcie_readl(dev, CSR_CTRL_SCRATCH_ADDR);
		
		end_ns = ktime_get_ns();
		
		/* Verify data integrity */
		if (readback != test_val) {
			local_irq_restore(flags);
			dev_err(&dev->dev->dev, "Latency test data mismatch at iteration %u: "
			        "wrote 0x%08x, read 0x%08x\n", i, test_val, readback);
			return -EIO;
		}
		
		/* Calculate latency */
		latency_ns = end_ns - start_ns;
		total_ns += latency_ns;
		
		if (latency_ns < min_ns)
			min_ns = latency_ns;
		if (latency_ns > max_ns)
			max_ns = latency_ns;
	}
	
	/* Re-enable interrupts */
	local_irq_restore(flags);
	
	/* Return results */
	lat->min_ns = min_ns;
	lat->max_ns = max_ns;
	lat->avg_ns = total_ns / lat->iterations;
	lat->total_ns = total_ns;
	
	dev_info(&dev->dev->dev, "Latency test complete: %u iterations, "
	         "min=%llu ns, avg=%llu ns, max=%llu ns\n",
	         lat->iterations, lat->min_ns, lat->avg_ns, lat->max_ns);
	
	return 0;
}