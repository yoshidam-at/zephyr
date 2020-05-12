/*
 * Copyright (c) 2017 Google LLC.
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <device.h>
#include <drivers/gpio.h>
#include <soc.h>
#include <drivers/interrupt_controller/sam0_eic.h>

#include "gpio_utils.h"

#ifndef PORT_PMUX_PMUXE_A_Val
#define PORT_PMUX_PMUXE_A_Val (0)
#endif

struct gpio_sam0_config {
	/* gpio_driver_config needs to be first */
	struct gpio_driver_config common;
	PortGroup *regs;
#ifdef CONFIG_SAM0_EIC
	u8_t id;
#endif
};

struct gpio_sam0_data {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common;
	gpio_port_pins_t debounce;
#ifdef CONFIG_SAM0_EIC
	sys_slist_t callbacks;
#endif
};

#define DEV_CFG(dev) \
	((const struct gpio_sam0_config *const)(dev)->config->config_info)
#define DEV_DATA(dev) \
	((struct gpio_sam0_data *const)(dev)->driver_data)

#ifdef CONFIG_SAM0_EIC
static void gpio_sam0_isr(u32_t pins, void *arg)
{
	struct device *const dev = (struct device *) arg;
	struct gpio_sam0_data *const data = DEV_DATA(dev);

	gpio_fire_callbacks(&data->callbacks, dev, pins);
}
#endif

static int gpio_sam0_config(struct device *dev, gpio_pin_t pin,
			    gpio_flags_t flags)
{
	const struct gpio_sam0_config *config = DEV_CFG(dev);
	PortGroup *regs = config->regs;
	PORT_PINCFG_Type pincfg = {
		.reg = 0,
	};

	if ((flags & GPIO_SINGLE_ENDED) != 0) {
		return -ENOTSUP;
	}

	/* Supports disconnected, input, output, or bidirectional */
	if ((flags & GPIO_INPUT) != 0) {
		pincfg.bit.INEN = 1;
	}
	if ((flags & GPIO_OUTPUT) != 0) {
		/* Output is incompatible with pull */
		if ((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) != 0) {
			return -ENOTSUP;
		}
		/* Bidirectional is supported */
		if ((flags & GPIO_OUTPUT_INIT_LOW) != 0) {
			regs->OUTCLR.reg = BIT(pin);
		} else if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0) {
			regs->OUTSET.reg = BIT(pin);
		}
		regs->DIRSET.reg = BIT(pin);
	} else {
		/* Not output, may be input */
		regs->DIRCLR.reg = BIT(pin);

		/* Pull configuration is supported if not output */
		if ((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) != 0) {
			pincfg.bit.PULLEN = 1;
			if ((flags & GPIO_PULL_UP) != 0) {
				regs->OUTSET.reg = BIT(pin);
			} else {
				regs->OUTCLR.reg = BIT(pin);
			}
		}
	}

	/* Preserve debounce flag for interrupt configuration. */
	WRITE_BIT(DEV_DATA(dev)->debounce, pin,
		  ((flags & GPIO_INT_DEBOUNCE) != 0)
		  && (pincfg.bit.INEN != 0));

	/* Write the now-built pin configuration */
	regs->PINCFG[pin] = pincfg;

	return 0;
}

static int gpio_sam0_port_get_raw(struct device *dev,
				  gpio_port_value_t *value)
{
	const struct gpio_sam0_config *config = DEV_CFG(dev);

	*value = config->regs->IN.reg;

	return 0;
}

static int gpio_sam0_port_set_masked_raw(struct device *dev,
					 gpio_port_pins_t mask,
					 gpio_port_value_t value)
{
	const struct gpio_sam0_config *config = DEV_CFG(dev);
	u32_t out = config->regs->OUT.reg;

	config->regs->OUT.reg = (out & ~mask) | (value & mask);

	return 0;
}

static int gpio_sam0_port_set_bits_raw(struct device *dev,
				       gpio_port_pins_t pins)
{
	const struct gpio_sam0_config *config = DEV_CFG(dev);

	config->regs->OUTSET.reg = pins;

	return 0;
}

static int gpio_sam0_port_clear_bits_raw(struct device *dev,
					 gpio_port_pins_t pins)
{
	const struct gpio_sam0_config *config = DEV_CFG(dev);

	config->regs->OUTCLR.reg = pins;

	return 0;
}

static int gpio_sam0_port_toggle_bits(struct device *dev,
				      gpio_port_pins_t pins)
{
	const struct gpio_sam0_config *config = DEV_CFG(dev);

	config->regs->OUTTGL.reg = pins;

	return 0;
}

#ifdef CONFIG_SAM0_EIC

static int gpio_sam0_pin_interrupt_configure(struct device *dev,
					     gpio_pin_t pin,
					     enum gpio_int_mode mode,
					     enum gpio_int_trig trig)
{
	const struct gpio_sam0_config *config = DEV_CFG(dev);
	PortGroup *regs = config->regs;
	PORT_PINCFG_Type pincfg = {
		.reg = regs->PINCFG[pin].reg,
	};
	enum sam0_eic_trigger trigger;
	int rc = 0;

	switch (mode) {
	case GPIO_INT_MODE_DISABLED:
		pincfg.bit.PMUXEN = 0;
		rc = sam0_eic_disable_interrupt(config->id, pin);
		if (rc == -EBUSY) {
			/* Ignore diagnostic disabling disabled */
			rc = 0;
		}
		if (rc == 0) {
			rc = sam0_eic_release(config->id, pin);
		}
		break;
	case GPIO_INT_MODE_LEVEL:
	case GPIO_INT_MODE_EDGE:
		/* Enabling interrupts on a pin requires disconnecting
		 * the pin from the I/O pin controller (PORT) module
		 * and connecting it to the External Interrupt
		 * Controller (EIC).  This would prevent using the pin
		 * as an output, so interrupts are only supported if
		 * the pin is configured as input-only.
		 */
		if ((pincfg.bit.INEN == 0)
		    || ((regs->DIR.reg & BIT(pin)) != 0)) {
			rc = -ENOTSUP;
			break;
		}

		/* Transfer control to EIC */
		pincfg.bit.PMUXEN = 1;
		if ((pin & 1U) != 0) {
			regs->PMUX[pin / 2U].bit.PMUXO = PORT_PMUX_PMUXE_A_Val;
		} else {
			regs->PMUX[pin / 2U].bit.PMUXE = PORT_PMUX_PMUXE_A_Val;
		}

		switch (trig) {
		case GPIO_INT_TRIG_LOW:
			trigger = (mode == GPIO_INT_MODE_LEVEL)
				? SAM0_EIC_LOW
				: SAM0_EIC_FALLING;
			break;
		case GPIO_INT_TRIG_HIGH:
			trigger = (mode == GPIO_INT_MODE_LEVEL)
				? SAM0_EIC_HIGH
				: SAM0_EIC_RISING;
			break;
		case GPIO_INT_TRIG_BOTH:
			trigger = SAM0_EIC_BOTH;
			break;
		default:
			rc = -EINVAL;
			break;
		}

		if (rc == 0) {
			rc = sam0_eic_acquire(config->id, pin, trigger,
					      (DEV_DATA(dev)->debounce & BIT(pin)) != 0,
					      gpio_sam0_isr, dev);
		}
		if (rc == 0) {
			rc = sam0_eic_enable_interrupt(config->id, pin);
		}

		break;
	default:
		rc = -EINVAL;
		break;
	}

	if (rc == 0) {
		/* Update the pin configuration */
		regs->PINCFG[pin] = pincfg;
	}

	return rc;
}


static int gpio_sam0_manage_callback(struct device *dev,
			      struct gpio_callback *callback, bool set)
{
	struct gpio_sam0_data *const data = DEV_DATA(dev);

	return gpio_manage_callback(&data->callbacks, callback, set);
}

int gpio_sam0_enable_callback(struct device *dev, gpio_pin_t pin)
{
	const struct gpio_sam0_config *config = DEV_CFG(dev);

	return sam0_eic_enable_interrupt(config->id, pin);
}

int gpio_sam0_disable_callback(struct device *dev, gpio_pin_t pin)
{
	const struct gpio_sam0_config *config = DEV_CFG(dev);

	return sam0_eic_disable_interrupt(config->id, pin);
}

static u32_t gpio_sam0_get_pending_int(struct device *dev)
{
	const struct gpio_sam0_config *config = DEV_CFG(dev);

	return sam0_eic_interrupt_pending(config->id);
}

#endif

static const struct gpio_driver_api gpio_sam0_api = {
	.pin_configure = gpio_sam0_config,
	.port_get_raw = gpio_sam0_port_get_raw,
	.port_set_masked_raw = gpio_sam0_port_set_masked_raw,
	.port_set_bits_raw = gpio_sam0_port_set_bits_raw,
	.port_clear_bits_raw = gpio_sam0_port_clear_bits_raw,
	.port_toggle_bits = gpio_sam0_port_toggle_bits,
#ifdef CONFIG_SAM0_EIC
	.pin_interrupt_configure = gpio_sam0_pin_interrupt_configure,
	.manage_callback = gpio_sam0_manage_callback,
	.enable_callback = gpio_sam0_enable_callback,
	.disable_callback = gpio_sam0_disable_callback,
	.get_pending_int = gpio_sam0_get_pending_int,
#endif
};

static int gpio_sam0_init(struct device *dev) { return 0; }

/* Port A */
#if DT_ATMEL_SAM0_GPIO_PORT_A_BASE_ADDRESS

static const struct gpio_sam0_config gpio_sam0_config_0 = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_INST_0_ATMEL_SAM0_GPIO_NGPIOS),
	},
	.regs = (PortGroup *)DT_ATMEL_SAM0_GPIO_PORT_A_BASE_ADDRESS,
#ifdef CONFIG_SAM0_EIC
	.id = 0,
#endif
};

static struct gpio_sam0_data gpio_sam0_data_0;

DEVICE_AND_API_INIT(gpio_sam0_0, DT_ATMEL_SAM0_GPIO_PORT_A_LABEL,
		    gpio_sam0_init, &gpio_sam0_data_0, &gpio_sam0_config_0,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &gpio_sam0_api);
#endif

/* Port B */
#if DT_ATMEL_SAM0_GPIO_PORT_B_BASE_ADDRESS

static const struct gpio_sam0_config gpio_sam0_config_1 = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_INST_1_ATMEL_SAM0_GPIO_NGPIOS),
	},
	.regs = (PortGroup *)DT_ATMEL_SAM0_GPIO_PORT_B_BASE_ADDRESS,
#ifdef CONFIG_SAM0_EIC
	.id = 1,
#endif
};

static struct gpio_sam0_data gpio_sam0_data_1;

DEVICE_AND_API_INIT(gpio_sam0_1, DT_ATMEL_SAM0_GPIO_PORT_B_LABEL,
		    gpio_sam0_init, &gpio_sam0_data_1, &gpio_sam0_config_1,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &gpio_sam0_api);
#endif

/* Port C */
#if DT_ATMEL_SAM0_GPIO_PORT_C_BASE_ADDRESS

static const struct gpio_sam0_config gpio_sam0_config_2 = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_INST_2_ATMEL_SAM0_GPIO_NGPIOS),
	},
	.regs = (PortGroup *)DT_ATMEL_SAM0_GPIO_PORT_C_BASE_ADDRESS,
#ifdef CONFIG_SAM0_EIC
	.id = 2,
#endif
};

static struct gpio_sam0_data gpio_sam0_data_2;

DEVICE_AND_API_INIT(gpio_sam0_2, DT_ATMEL_SAM0_GPIO_PORT_C_LABEL,
		    gpio_sam0_init, &gpio_sam0_data_2, &gpio_sam0_config_2,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &gpio_sam0_api);
#endif

/* Port D */
#if DT_ATMEL_SAM0_GPIO_PORT_D_BASE_ADDRESS

static const struct gpio_sam0_config gpio_sam0_config_3 = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_INST_3_ATMEL_SAM0_GPIO_NGPIOS),
	},
	.regs = (PortGroup *)DT_ATMEL_SAM0_GPIO_PORT_D_BASE_ADDRESS,
#ifdef CONFIG_SAM0_EIC
	.id = 3,
#endif
};

static struct gpio_sam0_data gpio_sam0_data_3;

DEVICE_AND_API_INIT(gpio_sam0_3, DT_ATMEL_SAM0_GPIO_PORT_D_LABEL,
		    gpio_sam0_init, &gpio_sam0_data_3, &gpio_sam0_config_3,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &gpio_sam0_api);
#endif
