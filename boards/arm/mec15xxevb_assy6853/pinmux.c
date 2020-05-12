/*
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <device.h>
#include <init.h>
#include <kernel.h>
#include <drivers/pinmux.h>

#include "soc.h"

static int board_pinmux_init(struct device *dev)
{
	ARG_UNUSED(dev);

#ifdef CONFIG_PINMUX_XEC_GPIO000_036
	struct device *porta =
		device_get_binding(CONFIG_PINMUX_XEC_GPIO000_036_NAME);
#endif
#ifdef CONFIG_PINMUX_XEC_GPIO040_076
	struct device *portb =
		device_get_binding(CONFIG_PINMUX_XEC_GPIO040_076_NAME);
#endif
#ifdef CONFIG_PINMUX_XEC_GPIO100_136
	struct device *portc =
		device_get_binding(CONFIG_PINMUX_XEC_GPIO100_136_NAME);
#endif
#ifdef CONFIG_PINMUX_XEC_GPIO140_176
	struct device *portd =
		device_get_binding(CONFIG_PINMUX_XEC_GPIO140_176_NAME);
#endif
#ifdef CONFIG_PINMUX_XEC_GPIO200_236
	struct device *porte =
		device_get_binding(CONFIG_PINMUX_XEC_GPIO200_236_NAME);
#endif
#ifdef CONFIG_PINMUX_XEC_GPIO240_276
	struct device *portf =
		device_get_binding(CONFIG_PINMUX_XEC_GPIO240_276_NAME);
#endif

	/* Release JTAG TDI and JTAG TDO pins so they can be
	 * controlled by their respective PCR register (UART2).
	 * For more details see table 44-1
	 */
	ECS_REGS->DEBUG_CTRL = (MCHP_ECS_DCTRL_DBG_EN |
				MCHP_ECS_DCTRL_MODE_SWD);

	/* See table 2-4 from the data sheet for pin multiplexing*/
#ifdef CONFIG_UART_NS16550_PORT_2
	/* Set muxing, for UART 2 TX/RX and power up */
	mchp_pcr_periph_slp_ctrl(PCR_UART2, MCHP_PCR_SLEEP_DIS);

	UART2_REGS->CFG_SEL = (MCHP_UART_LD_CFG_INTCLK +
		MCHP_UART_LD_CFG_RESET_SYS + MCHP_UART_LD_CFG_NO_INVERT);
	UART2_REGS->ACTV = MCHP_UART_LD_ACTIVATE;

	pinmux_pin_set(portd, MCHP_GPIO_145, MCHP_GPIO_CTRL_MUX_F2);
	pinmux_pin_set(portd, MCHP_GPIO_146, MCHP_GPIO_CTRL_MUX_F2);
#endif

#ifdef CONFIG_I2C_XEC_0
	/* Set muxing, for I2C0 - SMB00 */
	pinmux_pin_set(porta, MCHP_GPIO_003, MCHP_GPIO_CTRL_MUX_F1);
	pinmux_pin_set(porta, MCHP_GPIO_004, MCHP_GPIO_CTRL_MUX_F1);
#endif

#ifdef CONFIG_I2C_XEC_1
	/* Set muxing for I2C1 - SMB01 */
	pinmux_pin_set(portc, MCHP_GPIO_130, MCHP_GPIO_CTRL_MUX_F1);
	pinmux_pin_set(portc, MCHP_GPIO_131, MCHP_GPIO_CTRL_MUX_F1);
#endif

#ifdef CONFIG_I2C_XEC_2
	/* Set muxing, for I2C2 - SMB04 */
	pinmux_pin_set(portd, MCHP_GPIO_143, MCHP_GPIO_CTRL_MUX_F1);
	pinmux_pin_set(portd, MCHP_GPIO_144, MCHP_GPIO_CTRL_MUX_F1);
#endif

#ifdef CONFIG_ESPI_XEC
	mchp_pcr_periph_slp_ctrl(PCR_ESPI, MCHP_PCR_SLEEP_DIS);
	/* ESPI RESET */
	pinmux_pin_set(portb, MCHP_GPIO_061, MCHP_GPIO_CTRL_MUX_F1);
	/* ESPI ALERT */
	pinmux_pin_set(portb, MCHP_GPIO_063, MCHP_GPIO_CTRL_MUX_F1);
	/* ESPI CS */
	pinmux_pin_set(portb, MCHP_GPIO_066, MCHP_GPIO_CTRL_MUX_F1);
	/* ESPI CLK */
	pinmux_pin_set(portb, MCHP_GPIO_065, MCHP_GPIO_CTRL_MUX_F1);
	/* ESPI IO1-4*/
	pinmux_pin_set(portb, MCHP_GPIO_070, MCHP_GPIO_CTRL_MUX_F1);
	pinmux_pin_set(portb, MCHP_GPIO_071, MCHP_GPIO_CTRL_MUX_F1);
	pinmux_pin_set(portb, MCHP_GPIO_072, MCHP_GPIO_CTRL_MUX_F1);
	pinmux_pin_set(portb, MCHP_GPIO_073, MCHP_GPIO_CTRL_MUX_F1);
#endif
	return 0;
}

SYS_INIT(board_pinmux_init, PRE_KERNEL_1, CONFIG_PINMUX_INIT_PRIORITY);
