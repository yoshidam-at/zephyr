/*
 * Copyright (c) 2019 Carlo Caione <ccaione@baylibre.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM64 specific kernel interface header
 *
 * This header contains the ARM64 specific kernel interface.  It is
 * included by the kernel interface architecture-abstraction header
 * (include/arm/aarch64/cpu.h)
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_AARCH64_ARCH_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_AARCH64_ARCH_H_

/* Add include for DTS generated information */
#include <devicetree.h>

#include <arch/arm/aarch64/thread.h>
#include <arch/arm/aarch64/exc.h>
#include <arch/arm/aarch64/irq.h>
#include <arch/arm/aarch64/misc.h>
#include <arch/arm/aarch64/asm_inline.h>
#include <arch/arm/aarch64/cpu.h>
#include <arch/arm/aarch64/sys_io.h>
#include <arch/arm/aarch64/timer.h>
#include <arch/common/addr_types.h>
#include <arch/common/ffs.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Declare the STACK_ALIGN_SIZE
 *
 * Denotes the required alignment of the stack pointer on public API
 * boundaries
 *
 */
#define STACK_ALIGN		16
#define STACK_ALIGN_SIZE	STACK_ALIGN

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_AARCH64_ARCH_H_ */
