/*
 * Copyright (c) 2016, Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>

#include <device.h>
#include <drivers/gpio.h>
#include <init.h>
#include <kernel.h>
#include <sys/sys_io.h>

/* Driverlib includes */
#include <inc/hw_types.h>
#include <inc/hw_memmap.h>
#include <inc/hw_ints.h>
#include <inc/hw_gpio.h>
#include <driverlib/rom.h>
#include <driverlib/pin.h>
#undef __GPIO_H__  /* Zephyr and CC32XX SDK gpio.h conflict */
#include <driverlib/gpio.h>
#include <driverlib/rom_map.h>
#include <driverlib/interrupt.h>

#include "gpio_utils.h"

/* Reserved */
#define PIN_XX  0xFF

static const u8_t pinTable[] = {
	/* 00     01      02      03      04      05      06      07  */
	PIN_50, PIN_55, PIN_57, PIN_58, PIN_59, PIN_60, PIN_61, PIN_62,
	/* 08     09      10      11      12      13      14      15  */
	PIN_63, PIN_64, PIN_01, PIN_02, PIN_03, PIN_04, PIN_05, PIN_06,
	/* 16     17      18      19      20      21      22      23  */
	PIN_07, PIN_08, PIN_XX, PIN_XX, PIN_XX, PIN_XX, PIN_15, PIN_16,
	/* 24     25      26      27      28      29      30      31  */
	PIN_17, PIN_21, PIN_29, PIN_30, PIN_18, PIN_20, PIN_53, PIN_45,
	/* 32 */
	PIN_52
};

struct gpio_cc32xx_config {
	/* gpio_driver_config needs to be first */
	struct gpio_driver_config common;
	/* base address of GPIO port */
	unsigned long port_base;
	/* GPIO IRQ number */
	unsigned long irq_num;
	/* GPIO port number */
	u8_t port_num;
};

struct gpio_cc32xx_data {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common;
	/* list of registered callbacks */
	sys_slist_t callbacks;
	/* callback enable pin bitmask */
	u32_t pin_callback_enables;
};

#define DEV_CFG(dev) \
	((const struct gpio_cc32xx_config *)(dev)->config->config_info)
#define DEV_DATA(dev) \
	((struct gpio_cc32xx_data *)(dev)->driver_data)

static int gpio_cc32xx_port_set_bits_raw(struct device *port, u32_t mask);
static int gpio_cc32xx_port_clear_bits_raw(struct device *port, u32_t mask);

static inline int gpio_cc32xx_config(struct device *port,
				     gpio_pin_t pin,
				     gpio_flags_t flags)
{
	const struct gpio_cc32xx_config *gpio_config = DEV_CFG(port);
	unsigned long port_base = gpio_config->port_base;

	if (((flags & GPIO_INPUT) != 0) && ((flags & GPIO_OUTPUT) != 0)) {
		return -ENOTSUP;
	}

	if ((flags & (GPIO_INPUT | GPIO_OUTPUT)) == 0) {
		return -ENOTSUP;
	}

	if ((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) != 0) {
		return -ENOTSUP;
	}

	MAP_PinTypeGPIO(pinTable[gpio_config->port_num * 8 + pin],
		PIN_MODE_0, false);
	if (flags & GPIO_OUTPUT) {
		MAP_GPIODirModeSet(port_base, (1 << pin), GPIO_DIR_MODE_OUT);
		if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0) {
			gpio_cc32xx_port_set_bits_raw(port, BIT(pin));
		} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0) {
			gpio_cc32xx_port_clear_bits_raw(port, BIT(pin));
		}
	} else {
		MAP_GPIODirModeSet(port_base, (1 << pin), GPIO_DIR_MODE_IN);
	}

	return 0;
}

static int gpio_cc32xx_port_get_raw(struct device *port, u32_t *value)
{
	const struct gpio_cc32xx_config *gpio_config = DEV_CFG(port);
	unsigned long port_base = gpio_config->port_base;
	unsigned char pin_packed = 0xFF;

	*value = MAP_GPIOPinRead(port_base, pin_packed);

	return 0;
}

static int gpio_cc32xx_port_set_masked_raw(struct device *port, u32_t mask,
					  u32_t value)
{
	const struct gpio_cc32xx_config *gpio_config = DEV_CFG(port);
	unsigned long port_base = gpio_config->port_base;

	MAP_GPIOPinWrite(port_base, (unsigned char)mask, (unsigned char)value);

	return 0;
}

static int gpio_cc32xx_port_set_bits_raw(struct device *port, u32_t mask)
{
	const struct gpio_cc32xx_config *gpio_config = DEV_CFG(port);
	unsigned long port_base = gpio_config->port_base;

	MAP_GPIOPinWrite(port_base, (unsigned char)mask, (unsigned char)mask);

	return 0;
}

static int gpio_cc32xx_port_clear_bits_raw(struct device *port, u32_t mask)
{
	const struct gpio_cc32xx_config *gpio_config = DEV_CFG(port);
	unsigned long port_base = gpio_config->port_base;

	MAP_GPIOPinWrite(port_base, (unsigned char)mask, (unsigned char)~mask);

	return 0;
}

static int gpio_cc32xx_port_toggle_bits(struct device *port, u32_t mask)
{
	const struct gpio_cc32xx_config *gpio_config = DEV_CFG(port);
	unsigned long port_base = gpio_config->port_base;
	long value;

	value = MAP_GPIOPinRead(port_base, mask);

	MAP_GPIOPinWrite(port_base, (unsigned char)mask,
		(unsigned char)~value);

	return 0;
}

static int gpio_cc32xx_pin_interrupt_configure(struct device *port,
		gpio_pin_t pin, enum gpio_int_mode mode,
		enum gpio_int_trig trig)
{
	const struct gpio_cc32xx_config *gpio_config = DEV_CFG(port);
	struct gpio_cc32xx_data *data = DEV_DATA(port);
	unsigned long port_base = gpio_config->port_base;
	unsigned long int_type;

	__ASSERT(pin < 8, "Invalid pin number - only 8 pins per port");

	/*
	 * disable interrupt prior to changing int type helps
	 * prevent spurious interrupts observed when switching
	 * to level-based
	 */
	MAP_GPIOIntDisable(port_base, (1 << pin));

	if (mode != GPIO_INT_MODE_DISABLED) {
		if (mode == GPIO_INT_MODE_EDGE) {
			if (trig == GPIO_INT_TRIG_BOTH) {
				int_type = GPIO_BOTH_EDGES;
			} else if (trig == GPIO_INT_TRIG_HIGH) {
				int_type = GPIO_RISING_EDGE;
			} else {
				int_type = GPIO_FALLING_EDGE;
			}
		} else { /* GPIO_INT_MODE_LEVEL */
			if (trig == GPIO_INT_TRIG_HIGH) {
				int_type = GPIO_HIGH_LEVEL;
			} else {
				int_type = GPIO_LOW_LEVEL;
			}
		}

		MAP_GPIOIntTypeSet(port_base, (1 << pin), int_type);
		MAP_GPIOIntClear(port_base, (1 << pin));
		MAP_GPIOIntEnable(port_base, (1 << pin));

		WRITE_BIT(data->pin_callback_enables, pin,
			mode != GPIO_INT_MODE_DISABLED);
	}

	return 0;
}

static int gpio_cc32xx_manage_callback(struct device *dev,
				    struct gpio_callback *callback, bool set)
{
	struct gpio_cc32xx_data *data = DEV_DATA(dev);

	return gpio_manage_callback(&data->callbacks, callback, set);
}


static int gpio_cc32xx_enable_callback(struct device *dev,
				    gpio_pin_t pin)
{
	struct gpio_cc32xx_data *data = DEV_DATA(dev);

	__ASSERT(pin < 8, "Invalid pin number - only 8 pins per port");

	data->pin_callback_enables |= (1 << pin);

	return 0;
}


static int gpio_cc32xx_disable_callback(struct device *dev,
				     gpio_pin_t pin)
{
	struct gpio_cc32xx_data *data = DEV_DATA(dev);

	__ASSERT(pin < 8, "Invalid pin number - only 8 pins per port");

	data->pin_callback_enables &= ~(1 << pin);

	return 0;
}

static void gpio_cc32xx_port_isr(void *arg)
{
	struct device *dev = arg;
	const struct gpio_cc32xx_config *config = DEV_CFG(dev);
	struct gpio_cc32xx_data *data = DEV_DATA(dev);
	u32_t enabled_int, int_status;

	/* See which interrupts triggered: */
	int_status  = (u32_t)MAP_GPIOIntStatus(config->port_base, 1);

	enabled_int = int_status & data->pin_callback_enables;

	/* Clear GPIO Interrupt */
	MAP_GPIOIntClear(config->port_base, int_status);

	/* Call the registered callbacks */
	gpio_fire_callbacks(&data->callbacks, (struct device *)dev,
			     enabled_int);
}

static const struct gpio_driver_api api_funcs = {
	.pin_configure = gpio_cc32xx_config,
	.port_get_raw = gpio_cc32xx_port_get_raw,
	.port_set_masked_raw = gpio_cc32xx_port_set_masked_raw,
	.port_set_bits_raw = gpio_cc32xx_port_set_bits_raw,
	.port_clear_bits_raw = gpio_cc32xx_port_clear_bits_raw,
	.port_toggle_bits = gpio_cc32xx_port_toggle_bits,
	.pin_interrupt_configure = gpio_cc32xx_pin_interrupt_configure,
	.manage_callback = gpio_cc32xx_manage_callback,
	.enable_callback = gpio_cc32xx_enable_callback,
	.disable_callback = gpio_cc32xx_disable_callback,

};

#ifdef CONFIG_GPIO_CC32XX_A0
static const struct gpio_cc32xx_config gpio_cc32xx_a0_config = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_INST_0_TI_CC32XX_GPIO_NGPIOS),
	},
	.port_base = DT_GPIO_CC32XX_A0_BASE_ADDRESS,
	.irq_num = DT_GPIO_CC32XX_A0_IRQ+16,
	.port_num = 0
};

static struct device DEVICE_NAME_GET(gpio_cc32xx_a0);
static struct gpio_cc32xx_data gpio_cc32xx_a0_data;

static int gpio_cc32xx_a0_init(struct device *dev)
{
	ARG_UNUSED(dev);

	IRQ_CONNECT(DT_GPIO_CC32XX_A0_IRQ, DT_GPIO_CC32XX_A0_IRQ_PRI,
		    gpio_cc32xx_port_isr, DEVICE_GET(gpio_cc32xx_a0), 0);

	MAP_IntPendClear(DT_GPIO_CC32XX_A0_IRQ+16);
	irq_enable(DT_GPIO_CC32XX_A0_IRQ);

	return 0;
}

DEVICE_AND_API_INIT(gpio_cc32xx_a0, DT_GPIO_CC32XX_A0_NAME,
		    &gpio_cc32xx_a0_init, &gpio_cc32xx_a0_data,
		    &gpio_cc32xx_a0_config,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &api_funcs);

#endif

#ifdef CONFIG_GPIO_CC32XX_A1
static const struct gpio_cc32xx_config gpio_cc32xx_a1_config = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_INST_1_TI_CC32XX_GPIO_NGPIOS),
	},
	.port_base = DT_GPIO_CC32XX_A1_BASE_ADDRESS,
	.irq_num = DT_GPIO_CC32XX_A1_IRQ+16,
	.port_num = 1
};

static struct device DEVICE_NAME_GET(gpio_cc32xx_a1);
static struct gpio_cc32xx_data gpio_cc32xx_a1_data;

static int gpio_cc32xx_a1_init(struct device *dev)
{
	ARG_UNUSED(dev);

	IRQ_CONNECT(DT_GPIO_CC32XX_A1_IRQ, DT_GPIO_CC32XX_A1_IRQ_PRI,
		    gpio_cc32xx_port_isr, DEVICE_GET(gpio_cc32xx_a1), 0);

	MAP_IntPendClear(DT_GPIO_CC32XX_A1_IRQ+16);
	irq_enable(DT_GPIO_CC32XX_A1_IRQ);

	return 0;
}

DEVICE_AND_API_INIT(gpio_cc32xx_a1, DT_GPIO_CC32XX_A1_NAME,
		    &gpio_cc32xx_a1_init, &gpio_cc32xx_a1_data,
		    &gpio_cc32xx_a1_config,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &api_funcs);

#endif /* CONFIG_GPIO_CC32XX_A1 */

#ifdef CONFIG_GPIO_CC32XX_A2
static const struct gpio_cc32xx_config gpio_cc32xx_a2_config = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_INST_2_TI_CC32XX_GPIO_NGPIOS),
	},
	.port_base = DT_GPIO_CC32XX_A2_BASE_ADDRESS,
	.irq_num = DT_GPIO_CC32XX_A2_IRQ+16,
	.port_num = 2
};

static struct device DEVICE_NAME_GET(gpio_cc32xx_a2);
static struct gpio_cc32xx_data gpio_cc32xx_a2_data;

static int gpio_cc32xx_a2_init(struct device *dev)
{
	ARG_UNUSED(dev);

	IRQ_CONNECT(DT_GPIO_CC32XX_A2_IRQ, DT_GPIO_CC32XX_A2_IRQ_PRI,
		    gpio_cc32xx_port_isr, DEVICE_GET(gpio_cc32xx_a2), 0);

	MAP_IntPendClear(DT_GPIO_CC32XX_A2_IRQ+16);
	irq_enable(DT_GPIO_CC32XX_A2_IRQ);

	return 0;
}

DEVICE_AND_API_INIT(gpio_cc32xx_a2, DT_GPIO_CC32XX_A2_NAME,
		    &gpio_cc32xx_a2_init, &gpio_cc32xx_a2_data,
		    &gpio_cc32xx_a2_config,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &api_funcs);

#endif

#ifdef CONFIG_GPIO_CC32XX_A3
static const struct gpio_cc32xx_config gpio_cc32xx_a3_config = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_INST_3_TI_CC32XX_GPIO_NGPIOS),
	},
	.port_base = DT_GPIO_CC32XX_A3_BASE_ADDRESS,
	.irq_num = DT_GPIO_CC32XX_A3_IRQ+16,
	.port_num = 3
};

static struct device DEVICE_NAME_GET(gpio_cc32xx_a3);
static struct gpio_cc32xx_data gpio_cc32xx_a3_data;

static int gpio_cc32xx_a3_init(struct device *dev)
{
	ARG_UNUSED(dev);

	IRQ_CONNECT(DT_GPIO_CC32XX_A3_IRQ, DT_GPIO_CC32XX_A3_IRQ_PRI,
		    gpio_cc32xx_port_isr, DEVICE_GET(gpio_cc32xx_a3), 0);

	MAP_IntPendClear(DT_GPIO_CC32XX_A3_IRQ+16);
	irq_enable(DT_GPIO_CC32XX_A3_IRQ);

	return 0;
}

DEVICE_AND_API_INIT(gpio_cc32xx_a3, DT_GPIO_CC32XX_A3_NAME,
		    &gpio_cc32xx_a3_init, &gpio_cc32xx_a3_data,
		    &gpio_cc32xx_a3_config,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &api_funcs);

#endif
