/*
 * Copyright (c) 2018-2019, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <device.h>
#include <drivers/gpio.h>
#include <soc.h>
#include <sys/util.h>
#include <gpio_imx.h>

#include "gpio_utils.h"

struct imx_gpio_config {
	/* gpio_driver_config needs to be first */
	struct gpio_driver_config common;
	GPIO_Type *base;
};

struct imx_gpio_data {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common;
	/* port ISR callback routine address */
	sys_slist_t callbacks;
	/* pin callback routine enable flags, by pin number */
	u32_t pin_callback_enables;
};

static int imx_gpio_configure(struct device *port, gpio_pin_t pin,
			      gpio_flags_t flags)
{
	const struct imx_gpio_config *config = port->config->config_info;
	GPIO_Type *base = config->base;

	if (((flags & GPIO_INPUT) != 0U) && ((flags & GPIO_OUTPUT) != 0U)) {
		return -ENOTSUP;
	}

	if ((flags & (GPIO_SINGLE_ENDED
		      | GPIO_PULL_UP
		      | GPIO_PULL_DOWN)) != 0U) {
		return -ENOTSUP;
	}

	/* Disable interrupts for pin */
	GPIO_SetPinIntMode(base, pin, false);
	GPIO_SetIntEdgeSelect(base, pin, false);

	if ((flags & GPIO_OUTPUT) != 0U) {
		/* Set output pin initial value */
		if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
			GPIO_WritePinOutput(base, pin, gpioPinClear);
		} else if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
			GPIO_WritePinOutput(base, pin, gpioPinSet);
		}

		/* Set pin as output */
		WRITE_BIT(base->GDIR, pin, 1U);
	} else {
		/* Set pin as input */
		WRITE_BIT(base->GDIR, pin, 0U);
	}

	return 0;
}

static int imx_gpio_port_get_raw(struct device *port, u32_t *value)
{
	const struct imx_gpio_config *config = port->config->config_info;
	GPIO_Type *base = config->base;

	*value = GPIO_ReadPortInput(base);

	return 0;
}

static int imx_gpio_port_set_masked_raw(struct device *port,
					gpio_port_pins_t mask,
					gpio_port_value_t value)
{
	const struct imx_gpio_config *config = port->config->config_info;
	GPIO_Type *base = config->base;

	GPIO_WritePortOutput(base,
			(GPIO_ReadPortInput(base) & ~mask) | (value & mask));

	return 0;
}

static int imx_gpio_port_set_bits_raw(struct device *port,
				      gpio_port_pins_t pins)
{
	const struct imx_gpio_config *config = port->config->config_info;
	GPIO_Type *base = config->base;

	GPIO_WritePortOutput(base, GPIO_ReadPortInput(base) | pins);

	return 0;
}

static int imx_gpio_port_clear_bits_raw(struct device *port,
					gpio_port_pins_t pins)
{
	const struct imx_gpio_config *config = port->config->config_info;
	GPIO_Type *base = config->base;

	GPIO_WritePortOutput(base, GPIO_ReadPortInput(base) & ~pins);

	return 0;
}

static int imx_gpio_port_toggle_bits(struct device *port, gpio_port_pins_t pins)
{
	const struct imx_gpio_config *config = port->config->config_info;
	GPIO_Type *base = config->base;

	GPIO_WritePortOutput(base, GPIO_ReadPortInput(base) ^ pins);

	return 0;
}

static int imx_gpio_pin_interrupt_configure(struct device *port,
					    gpio_pin_t pin,
					    enum gpio_int_mode mode,
					    enum gpio_int_trig trig)
{
	const struct imx_gpio_config *config = port->config->config_info;
	struct imx_gpio_data *data = port->driver_data;
	GPIO_Type *base = config->base;
	volatile u32_t *icr_reg;
	unsigned int key;
	u32_t icr_val;
	u8_t shift;

	if (((base->GDIR & BIT(pin)) != 0U)
	    && (mode != GPIO_INT_MODE_DISABLED)) {
		/* Interrupt on output pin not supported */
		return -ENOTSUP;
	}

	if ((mode == GPIO_INT_MODE_EDGE) && (trig == GPIO_INT_TRIG_LOW)) {
		icr_val = 3U;
	} else if ((mode == GPIO_INT_MODE_EDGE) &&
		   (trig == GPIO_INT_TRIG_HIGH)) {
		icr_val = 2U;
	} else if ((mode == GPIO_INT_MODE_LEVEL) &&
		   (trig == GPIO_INT_TRIG_HIGH)) {
		icr_val = 1U;
	} else {
		icr_val = 0U;
	}

	if (pin < 16U) {
		shift = 2U * pin;
		icr_reg = &(base->ICR1);
	} else if (pin < 32U) {
		shift = 2U * (pin - 16U);
		icr_reg = &(base->ICR2);
	} else {
		return -EINVAL;
	}

	key = irq_lock();

	*icr_reg = (*icr_reg & ~(3U << shift)) | (icr_val << shift);

	WRITE_BIT(base->EDGE_SEL, pin, trig == GPIO_INT_TRIG_BOTH);
	WRITE_BIT(base->ISR, pin, mode != GPIO_INT_MODE_DISABLED);
	WRITE_BIT(base->IMR, pin, mode != GPIO_INT_MODE_DISABLED);
	WRITE_BIT(data->pin_callback_enables, pin,
		  mode != GPIO_INT_MODE_DISABLED);

	irq_unlock(key);

	return 0;
}

static int imx_gpio_manage_callback(struct device *port,
				    struct gpio_callback *cb, bool set)
{
	struct imx_gpio_data *data = port->driver_data;

	return gpio_manage_callback(&data->callbacks, cb, set);
}

static int imx_gpio_enable_callback(struct device *port,
				    gpio_pin_t pin)
{
	const struct imx_gpio_config *config = port->config->config_info;
	struct imx_gpio_data *data = port->driver_data;

	data->pin_callback_enables |= BIT(pin);
	GPIO_SetPinIntMode(config->base, pin, true);

	return 0;
}

static int imx_gpio_disable_callback(struct device *port,
				     gpio_pin_t pin)
{
	const struct imx_gpio_config *config = port->config->config_info;
	struct imx_gpio_data *data = port->driver_data;

	GPIO_SetPinIntMode(config->base, pin, false);
	data->pin_callback_enables &= ~BIT(pin);

	return 0;
}

static void imx_gpio_port_isr(void *arg)
{
	struct device *port = (struct device *)arg;
	const struct imx_gpio_config *config = port->config->config_info;
	struct imx_gpio_data *data = port->driver_data;
	u32_t enabled_int;

	enabled_int = config->base->ISR & data->pin_callback_enables;
	config->base->ISR = enabled_int;

	gpio_fire_callbacks(&data->callbacks, port, enabled_int);
}

static const struct gpio_driver_api imx_gpio_driver_api = {
	.pin_configure = imx_gpio_configure,
	.port_get_raw = imx_gpio_port_get_raw,
	.port_set_masked_raw = imx_gpio_port_set_masked_raw,
	.port_set_bits_raw = imx_gpio_port_set_bits_raw,
	.port_clear_bits_raw = imx_gpio_port_clear_bits_raw,
	.port_toggle_bits = imx_gpio_port_toggle_bits,
	.pin_interrupt_configure = imx_gpio_pin_interrupt_configure,
	.manage_callback = imx_gpio_manage_callback,
	.enable_callback = imx_gpio_enable_callback,
	.disable_callback = imx_gpio_disable_callback,
};

#ifdef CONFIG_GPIO_IMX_PORT_1
static int imx_gpio_1_init(struct device *port);

static const struct imx_gpio_config imx_gpio_1_config = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_GPIO_IMX_PORT_1_NGPIOS),
	},
	.base = (GPIO_Type *)DT_GPIO_IMX_PORT_1_BASE_ADDRESS,
};

static struct imx_gpio_data imx_gpio_1_data;

DEVICE_AND_API_INIT(imx_gpio_1, DT_GPIO_IMX_PORT_1_NAME,
		    imx_gpio_1_init,
		    &imx_gpio_1_data, &imx_gpio_1_config,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		    &imx_gpio_driver_api);

static int imx_gpio_1_init(struct device *port)
{
	IRQ_CONNECT(DT_GPIO_IMX_PORT_1_IRQ_0,
		    DT_GPIO_IMX_PORT_1_IRQ_0_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_1), 0);

	irq_enable(DT_GPIO_IMX_PORT_1_IRQ_0);

	IRQ_CONNECT(DT_GPIO_IMX_PORT_1_IRQ_1,
		    DT_GPIO_IMX_PORT_1_IRQ_1_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_1), 0);

	irq_enable(DT_GPIO_IMX_PORT_1_IRQ_1);

	return 0;
}
#endif /* CONFIG_GPIO_IMX_PORT_1 */

#ifdef CONFIG_GPIO_IMX_PORT_2
static int imx_gpio_2_init(struct device *port);

static const struct imx_gpio_config imx_gpio_2_config = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_GPIO_IMX_PORT_2_NGPIOS),
	},
	.base = (GPIO_Type *)DT_GPIO_IMX_PORT_2_BASE_ADDRESS,
};

static struct imx_gpio_data imx_gpio_2_data;

DEVICE_AND_API_INIT(imx_gpio_2, DT_GPIO_IMX_PORT_2_NAME,
		    imx_gpio_2_init,
		    &imx_gpio_2_data, &imx_gpio_2_config,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		    &imx_gpio_driver_api);

static int imx_gpio_2_init(struct device *port)
{
	IRQ_CONNECT(DT_GPIO_IMX_PORT_2_IRQ_0,
		    DT_GPIO_IMX_PORT_2_IRQ_0_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_2), 0);

	irq_enable(DT_GPIO_IMX_PORT_2_IRQ_0);

	IRQ_CONNECT(DT_GPIO_IMX_PORT_2_IRQ_1,
		    DT_GPIO_IMX_PORT_2_IRQ_1_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_2), 0);

	irq_enable(DT_GPIO_IMX_PORT_2_IRQ_1);

	return 0;
}
#endif /* CONFIG_GPIO_IMX_PORT_2 */

#ifdef CONFIG_GPIO_IMX_PORT_3
static int imx_gpio_3_init(struct device *port);

static const struct imx_gpio_config imx_gpio_3_config = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_GPIO_IMX_PORT_3_NGPIOS),
	},
	.base = (GPIO_Type *)DT_GPIO_IMX_PORT_3_BASE_ADDRESS,
};

static struct imx_gpio_data imx_gpio_3_data;

DEVICE_AND_API_INIT(imx_gpio_3, DT_GPIO_IMX_PORT_3_NAME,
		    imx_gpio_3_init,
		    &imx_gpio_3_data, &imx_gpio_3_config,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		    &imx_gpio_driver_api);

static int imx_gpio_3_init(struct device *port)
{
	IRQ_CONNECT(DT_GPIO_IMX_PORT_3_IRQ_0,
		    DT_GPIO_IMX_PORT_3_IRQ_0_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_3), 0);

	irq_enable(DT_GPIO_IMX_PORT_3_IRQ_0);

	IRQ_CONNECT(DT_GPIO_IMX_PORT_3_IRQ_1,
		    DT_GPIO_IMX_PORT_3_IRQ_1_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_3), 0);

	irq_enable(DT_GPIO_IMX_PORT_3_IRQ_1);

	return 0;
}
#endif /* CONFIG_GPIO_IMX_PORT_3 */

#ifdef CONFIG_GPIO_IMX_PORT_4
static int imx_gpio_4_init(struct device *port);

static const struct imx_gpio_config imx_gpio_4_config = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_GPIO_IMX_PORT_4_NGPIOS),
	},
	.base = (GPIO_Type *)DT_GPIO_IMX_PORT_4_BASE_ADDRESS,
};

static struct imx_gpio_data imx_gpio_4_data;

DEVICE_AND_API_INIT(imx_gpio_4, DT_GPIO_IMX_PORT_4_NAME,
		    imx_gpio_4_init,
		    &imx_gpio_4_data, &imx_gpio_4_config,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		    &imx_gpio_driver_api);

static int imx_gpio_4_init(struct device *port)
{
	IRQ_CONNECT(DT_GPIO_IMX_PORT_4_IRQ_0,
		    DT_GPIO_IMX_PORT_4_IRQ_0_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_4), 0);

	irq_enable(DT_GPIO_IMX_PORT_4_IRQ_0);

	IRQ_CONNECT(DT_GPIO_IMX_PORT_4_IRQ_1,
		    DT_GPIO_IMX_PORT_4_IRQ_1_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_4), 0);

	irq_enable(DT_GPIO_IMX_PORT_4_IRQ_1);

	return 0;
}
#endif /* CONFIG_GPIO_IMX_PORT_4 */

#ifdef CONFIG_GPIO_IMX_PORT_5
static int imx_gpio_5_init(struct device *port);

static const struct imx_gpio_config imx_gpio_5_config = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_GPIO_IMX_PORT_5_NGPIOS),
	},
	.base = (GPIO_Type *)DT_GPIO_IMX_PORT_5_BASE_ADDRESS,
};

static struct imx_gpio_data imx_gpio_5_data;

DEVICE_AND_API_INIT(imx_gpio_5, DT_GPIO_IMX_PORT_5_NAME,
		    imx_gpio_5_init,
		    &imx_gpio_5_data, &imx_gpio_5_config,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		    &imx_gpio_driver_api);

static int imx_gpio_5_init(struct device *port)
{
	IRQ_CONNECT(DT_GPIO_IMX_PORT_5_IRQ_0,
		    DT_GPIO_IMX_PORT_5_IRQ_0_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_5), 0);

	irq_enable(DT_GPIO_IMX_PORT_5_IRQ_0);

	IRQ_CONNECT(DT_GPIO_IMX_PORT_5_IRQ_1,
		    DT_GPIO_IMX_PORT_5_IRQ_1_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_5), 0);

	irq_enable(DT_GPIO_IMX_PORT_5_IRQ_1);

	return 0;
}
#endif /* CONFIG_GPIO_IMX_PORT_5 */

#ifdef CONFIG_GPIO_IMX_PORT_6
static int imx_gpio_6_init(struct device *port);

static const struct imx_gpio_config imx_gpio_6_config = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_GPIO_IMX_PORT_6_NGPIOS),
	},
	.base = (GPIO_Type *)DT_GPIO_IMX_PORT_6_BASE_ADDRESS,
};

static struct imx_gpio_data imx_gpio_6_data;

DEVICE_AND_API_INIT(imx_gpio_6, DT_GPIO_IMX_PORT_6_NAME,
		    imx_gpio_6_init,
		    &imx_gpio_6_data, &imx_gpio_6_config,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		    &imx_gpio_driver_api);

static int imx_gpio_6_init(struct device *port)
{
	IRQ_CONNECT(DT_GPIO_IMX_PORT_6_IRQ_0,
		    DT_GPIO_IMX_PORT_6_IRQ_0_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_6), 0);

	irq_enable(DT_GPIO_IMX_PORT_6_IRQ_0);

	IRQ_CONNECT(DT_GPIO_IMX_PORT_6_IRQ_1,
		    DT_GPIO_IMX_PORT_6_IRQ_1_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_6), 0);

	irq_enable(DT_GPIO_IMX_PORT_6_IRQ_1);

	return 0;
}
#endif /* CONFIG_GPIO_IMX_PORT_6 */

#ifdef CONFIG_GPIO_IMX_PORT_7
static int imx_gpio_7_init(struct device *port);

static const struct imx_gpio_config imx_gpio_7_config = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_GPIO_IMX_PORT_7_NGPIOS),
	},
	.base = (GPIO_Type *)DT_GPIO_IMX_PORT_7_BASE_ADDRESS,
};

static struct imx_gpio_data imx_gpio_7_data;

DEVICE_AND_API_INIT(imx_gpio_7, DT_GPIO_IMX_PORT_7_NAME,
		    imx_gpio_7_init,
		    &imx_gpio_7_data, &imx_gpio_7_config,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		    &imx_gpio_driver_api);

static int imx_gpio_7_init(struct device *port)
{
	IRQ_CONNECT(DT_GPIO_IMX_PORT_7_IRQ_0,
		    DT_GPIO_IMX_PORT_7_IRQ_0_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_7), 0);

	irq_enable(DT_GPIO_IMX_PORT_7_IRQ_0);

	IRQ_CONNECT(DT_GPIO_IMX_PORT_7_IRQ_1,
		    DT_GPIO_IMX_PORT_7_IRQ_1_PRI,
		    imx_gpio_port_isr, DEVICE_GET(imx_gpio_7), 0);

	irq_enable(DT_GPIO_IMX_PORT_7_IRQ_1);

	return 0;
}
#endif /* CONFIG_GPIO_IMX_PORT_7 */
