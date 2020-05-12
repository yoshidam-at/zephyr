/*
 * Copyright (c) 2016 Wind River Systems, Inc.
 * Copyright (c) 2016 Cadence Design Systems, Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/* this file is only meant to be included by kernel_structs.h */

#ifndef ZEPHYR_ARCH_XTENSA_INCLUDE_KERNEL_ARCH_FUNC_H_
#define ZEPHYR_ARCH_XTENSA_INCLUDE_KERNEL_ARCH_FUNC_H_

#ifndef _ASMLANGUAGE
#include <kernel_arch_data.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* stack alignment related macros: STACK_ALIGN_SIZE is defined above */

#define STACK_ROUND_UP(x) ROUND_UP(x, STACK_ALIGN_SIZE)
#define STACK_ROUND_DOWN(x) ROUND_DOWN(x, STACK_ALIGN_SIZE)

extern void FatalErrorHandler(void);
extern void ReservedInterruptHandler(unsigned int intNo);
extern void z_xtensa_fatal_error(unsigned int reason, const z_arch_esf_t *esf);

/* Defined in xtensa_context.S */
extern void z_xt_coproc_init(void);

extern K_THREAD_STACK_DEFINE(_interrupt_stack, CONFIG_ISR_STACK_SIZE);

static ALWAYS_INLINE void arch_kernel_init(void)
{
	_cpu_t *cpu0 = &_kernel.cpus[0];

	cpu0->nested = 0;
	cpu0->irq_stack = (Z_THREAD_STACK_BUFFER(_interrupt_stack) +
			   CONFIG_ISR_STACK_SIZE);

	/* The asm2 scheme keeps the kernel pointer in MISC0 for easy
	 * access.  That saves 4 bytes of immediate value to store the
	 * address when compared to the legacy scheme.  But in SMP
	 * this record is a per-CPU thing and having it stored in a SR
	 * already is a big win.
	 */
	WSR(CONFIG_XTENSA_KERNEL_CPU_PTR_SR, cpu0);

#ifdef CONFIG_INIT_STACKS
	memset(Z_THREAD_STACK_BUFFER(_interrupt_stack), 0xAA,
	       CONFIG_ISR_STACK_SIZE);
#endif
}

void xtensa_switch(void *switch_to, void **switched_from);

static inline void arch_switch(void *switch_to, void **switched_from)
{
	return xtensa_switch(switch_to, switched_from);
}

#ifdef __cplusplus
}
#endif

static inline bool arch_is_in_isr(void)
{
	return arch_curr_cpu()->nested != 0U;
}
#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_ARCH_XTENSA_INCLUDE_KERNEL_ARCH_FUNC_H_ */
