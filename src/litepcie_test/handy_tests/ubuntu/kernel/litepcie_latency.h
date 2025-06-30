/* SPDX-License-Identifier: BSD-2-Clause
 *
 * LitePCIe Latency Measurement Extension
 *
 * This file is part of LitePCIe.
 *
 * Copyright (C) 2024 / Your Name
 *
 */

#ifndef _LITEPCIE_LATENCY_H
#define _LITEPCIE_LATENCY_H

#include <linux/types.h>

/* Latency test IOCTL structure */
struct litepcie_ioctl_latency {
	uint32_t iterations;      /* Number of measurements */
	uint64_t min_ns;         /* Minimum latency in nanoseconds */
	uint64_t max_ns;         /* Maximum latency in nanoseconds */
	uint64_t avg_ns;         /* Average latency in nanoseconds */
	uint64_t total_ns;       /* Total time for all iterations */
};

/* IOCTL command for latency test */
#define LITEPCIE_IOCTL_LATENCY_TEST _IOWR(LITEPCIE_IOCTL, 30, struct litepcie_ioctl_latency)

#endif /* _LITEPCIE_LATENCY_H */