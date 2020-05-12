/*
 * Copyright (c) 2019 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <syscall_handler.h>
#include <ptp_clock.h>

#ifdef CONFIG_USERSPACE
Z_SYSCALL_HANDLER(ptp_clock_get, dev, tm)
{
	struct net_ptp_time ptp_time;
	int ret;

	Z_OOPS(Z_SYSCALL_DRIVER_PTP_CLOCK(dev, get));
	Z_OOPS(Z_SYSCALL_MEMORY_WRITE(tm, sizeof(struct net_ptp_time)));

	ret = z_impl_ptp_clock_get((struct device *)dev, &ptp_time);
	if (ret != 0) {
		return 0;
	}

	if (z_user_to_copy((void *)tm, &ptp_time, sizeof(ptp_time)) != 0) {
		return 0;
	}

	return (u32_t)ret;
}
#endif /* CONFIG_USERSPACE */
