/*
 * Copyright (c) 2017, Christian Taedcke
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <drivers/gpio.h>
#include <soc.h>
#include <em_gpio.h>

#include "gpio_utils.h"

/*
 * Macros to set the GPIO MODE registers
 *
 * See https://www.silabs.com/documents/public/reference-manuals/EFM32WG-RM.pdf
 * pages 972 and 982.
 */
/**
 * @brief Create the value to set the GPIO MODEL register
 * @param[in] pin The index of the pin. Valid values are 0..7.
 * @param[in] mode The mode that should be set.
 * @return The value that can be set into the GPIO MODEL register.
 */
#define GECKO_GPIO_MODEL(pin, mode) (mode << (pin * 4))

/**
 * @brief Create the value to set the GPIO MODEH register
 * @param[in] pin The index of the pin. Valid values are 8..15.
 * @param[in] mode The mode that should be set.
 * @return The value that can be set into the GPIO MODEH register.
 */
#define GECKO_GPIO_MODEH(pin, mode) (mode << ((pin - 8) * 4))


#define member_size(type, member) sizeof(((type *)0)->member)
#define NUMBER_OF_PORTS (member_size(GPIO_TypeDef, P) / \
			 member_size(GPIO_TypeDef, P[0]))

struct gpio_gecko_common_config {
};

struct gpio_gecko_common_data {
	/* a list of all ports */
	struct device *ports[NUMBER_OF_PORTS];
	size_t count;
};

struct gpio_gecko_config {
	/* gpio_driver_config needs to be first */
	struct gpio_driver_config common;
	GPIO_P_TypeDef *gpio_base;
	GPIO_Port_TypeDef gpio_index;
};

struct gpio_gecko_data {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common;
	/* port ISR callback routine address */
	sys_slist_t callbacks;
	/* pin callback routine enable flags, by pin number */
	u32_t pin_callback_enables;
};

static inline void gpio_gecko_add_port(struct gpio_gecko_common_data *data,
				       struct device *dev)
{
	__ASSERT(dev, "No port device!");
	data->ports[data->count++] = dev;
}

static int gpio_gecko_configure(struct device *dev,
				gpio_pin_t pin,
				gpio_flags_t flags)
{
	const struct gpio_gecko_config *config = dev->config->config_info;
	GPIO_Port_TypeDef gpio_index = config->gpio_index;
	GPIO_Mode_TypeDef mode;
	unsigned int out = 0U;

	if (flags & GPIO_OUTPUT) {
		/* Following modes enable both output and input */
		if (flags & GPIO_SINGLE_ENDED) {
			if (flags & GPIO_LINE_OPEN_DRAIN) {
				mode = gpioModeWiredAnd;
			} else {
				mode = gpioModeWiredOr;
			}
		} else {
			mode = gpioModePushPull;
		}
		if (flags & GPIO_OUTPUT_INIT_HIGH) {
			out = 1U;
		} else if (flags & GPIO_OUTPUT_INIT_LOW) {
			out = 0U;
		} else {
			out = GPIO_PinOutGet(gpio_index, pin);
		}
	} else if (flags & GPIO_INPUT) {
		if (flags & GPIO_PULL_UP) {
			mode = gpioModeInputPull;
			out = 1U; /* pull-up*/
		} else if (flags & GPIO_PULL_DOWN) {
			mode = gpioModeInputPull;
			/* out = 0 means pull-down*/
		} else {
			mode = gpioModeInput;
		}
	} else {
		/* Neither input nor output mode is selected */
		mode = gpioModeDisabled;
	}
	/* The flags contain options that require touching registers in the
	 * GPIO module and the corresponding PORT module.
	 *
	 * Start with the GPIO module and set up the pin direction register.
	 * 0 - pin is input, 1 - pin is output
	 */

	GPIO_PinModeSet(gpio_index, pin, mode, out);

	return 0;
}

static int gpio_gecko_port_get_raw(struct device *dev, u32_t *value)
{
	const struct gpio_gecko_config *config = dev->config->config_info;
	GPIO_P_TypeDef *gpio_base = config->gpio_base;

	*value = gpio_base->DIN;

	return 0;
}

static int gpio_gecko_port_set_masked_raw(struct device *dev, u32_t mask,
					  u32_t value)
{
	const struct gpio_gecko_config *config = dev->config->config_info;
	GPIO_P_TypeDef *gpio_base = config->gpio_base;

	gpio_base->DOUT = (gpio_base->DOUT & ~mask) | (mask & value);

	return 0;
}

static int gpio_gecko_port_set_bits_raw(struct device *dev, u32_t mask)
{
	const struct gpio_gecko_config *config = dev->config->config_info;
	GPIO_P_TypeDef *gpio_base = config->gpio_base;

#if defined(_GPIO_P_DOUTSET_MASK)
	gpio_base->DOUTSET = mask;
#else
	BUS_RegMaskedSet(&gpio_base->DOUT, mask);
#endif

	return 0;
}

static int gpio_gecko_port_clear_bits_raw(struct device *dev, u32_t mask)
{
	const struct gpio_gecko_config *config = dev->config->config_info;
	GPIO_P_TypeDef *gpio_base = config->gpio_base;

#if defined(_GPIO_P_DOUTCLR_MASK)
	gpio_base->DOUTCLR = mask;
#else
	BUS_RegMaskedClear(&gpio_base->DOUT, mask);
#endif

	return 0;
}

static int gpio_gecko_port_toggle_bits(struct device *dev, u32_t mask)
{
	const struct gpio_gecko_config *config = dev->config->config_info;
	GPIO_P_TypeDef *gpio_base = config->gpio_base;

	gpio_base->DOUTTGL = mask;

	return 0;
}

static int gpio_gecko_pin_interrupt_configure(struct device *dev,
		gpio_pin_t pin, enum gpio_int_mode mode,
		enum gpio_int_trig trig)
{
	const struct gpio_gecko_config *config = dev->config->config_info;
	struct gpio_gecko_data *data = dev->driver_data;

	/* Interrupt on static level is not supported by the hardware */
	if (mode == GPIO_INT_MODE_LEVEL) {
		return -ENOTSUP;
	}

	if (mode == GPIO_INT_MODE_DISABLED) {
		GPIO_IntDisable(BIT(pin));
	} else {
		/* Interrupt line is already in use */
		if ((GPIO->IEN & BIT(pin)) != 0) {
			/* TODO: Return an error only if request is done for
			 * a pin from a different port.
			 */
			return -EBUSY;
		}

		bool rising_edge = true;
		bool falling_edge = true;

		if (trig == GPIO_INT_TRIG_LOW) {
			rising_edge = false;
			falling_edge = true;
		} else if (trig == GPIO_INT_TRIG_HIGH) {
			rising_edge = true;
			falling_edge = false;
		} /* default is GPIO_INT_TRIG_BOTH */

		GPIO_IntConfig(config->gpio_index, pin,
			       rising_edge, falling_edge, true);
	}

	WRITE_BIT(data->pin_callback_enables, pin, mode != GPIO_INT_DISABLE);

	return 0;
}

static int gpio_gecko_manage_callback(struct device *dev,
				      struct gpio_callback *callback, bool set)
{
	struct gpio_gecko_data *data = dev->driver_data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}

static int gpio_gecko_enable_callback(struct device *dev,
				      gpio_pin_t pin)
{
	struct gpio_gecko_data *data = dev->driver_data;

	data->pin_callback_enables |= BIT(pin);
	GPIO->IEN |= BIT(pin);

	return 0;
}

static int gpio_gecko_disable_callback(struct device *dev,
				       gpio_pin_t pin)
{
	struct gpio_gecko_data *data = dev->driver_data;

	data->pin_callback_enables &= ~BIT(pin);
	GPIO->IEN &= ~BIT(pin);

	return 0;
}

/**
 * Handler for both odd and even pin interrupts
 */
static void gpio_gecko_common_isr(void *arg)
{
	struct device *dev = (struct device *)arg;
	struct gpio_gecko_common_data *data = dev->driver_data;
	u32_t enabled_int, int_status;
	struct device *port_dev;
	struct gpio_gecko_data *port_data;

	int_status = GPIO->IF;

	for (unsigned int i = 0; int_status && (i < data->count); i++) {
		port_dev = data->ports[i];
		port_data = port_dev->driver_data;
		enabled_int = int_status & port_data->pin_callback_enables;
		if (enabled_int != 0) {
			int_status &= ~enabled_int;
			GPIO->IFC = enabled_int;
			gpio_fire_callbacks(&port_data->callbacks, port_dev,
					    enabled_int);
		}
	}
}


static const struct gpio_driver_api gpio_gecko_driver_api = {
	.pin_configure = gpio_gecko_configure,
	.port_get_raw = gpio_gecko_port_get_raw,
	.port_set_masked_raw = gpio_gecko_port_set_masked_raw,
	.port_set_bits_raw = gpio_gecko_port_set_bits_raw,
	.port_clear_bits_raw = gpio_gecko_port_clear_bits_raw,
	.port_toggle_bits = gpio_gecko_port_toggle_bits,
	.pin_interrupt_configure = gpio_gecko_pin_interrupt_configure,
	.manage_callback = gpio_gecko_manage_callback,
	.enable_callback = gpio_gecko_enable_callback,
	.disable_callback = gpio_gecko_disable_callback,
};

static const struct gpio_driver_api gpio_gecko_common_driver_api = {
	.manage_callback = gpio_gecko_manage_callback,
	.enable_callback = gpio_gecko_enable_callback,
	.disable_callback = gpio_gecko_disable_callback,
};

#ifdef CONFIG_GPIO_GECKO
static int gpio_gecko_common_init(struct device *dev);

static const struct gpio_gecko_common_config gpio_gecko_common_config = {
};

static struct gpio_gecko_common_data gpio_gecko_common_data;

DEVICE_AND_API_INIT(gpio_gecko_common, DT_GPIO_GECKO_COMMON_NAME,
		    gpio_gecko_common_init,
		    &gpio_gecko_common_data, &gpio_gecko_common_config,
		    POST_KERNEL, CONFIG_GPIO_GECKO_COMMON_INIT_PRIORITY,
		    &gpio_gecko_common_driver_api);

static int gpio_gecko_common_init(struct device *dev)
{
	gpio_gecko_common_data.count = 0;
	IRQ_CONNECT(GPIO_EVEN_IRQn, DT_GPIO_GECKO_COMMON_EVEN_PRI,
		    gpio_gecko_common_isr, DEVICE_GET(gpio_gecko_common), 0);

	IRQ_CONNECT(GPIO_ODD_IRQn, DT_GPIO_GECKO_COMMON_ODD_PRI,
		    gpio_gecko_common_isr, DEVICE_GET(gpio_gecko_common), 0);

	irq_enable(GPIO_EVEN_IRQn);
	irq_enable(GPIO_ODD_IRQn);

	return 0;
}
#endif /* CONFIG_GPIO_GECKO */

#define GPIO_PORT_INIT(pl, pu) \
static int gpio_gecko_port##pl##_init(struct device *dev); \
\
static const struct gpio_gecko_config gpio_gecko_port##pl##_config = { \
	.common = { \
		.port_pin_mask = (gpio_port_pins_t)(-1), \
	}, \
	.gpio_base = &GPIO->P[gpioPort##pu], \
	.gpio_index = gpioPort##pu, \
}; \
\
static struct gpio_gecko_data gpio_gecko_port##pl##_data; \
\
DEVICE_AND_API_INIT(gpio_gecko_port##pl, DT_GPIO_GECKO_PORT##pu##_NAME, \
		    gpio_gecko_port##pl##_init, \
		    &gpio_gecko_port##pl##_data, \
		    &gpio_gecko_port##pl##_config, \
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
		    &gpio_gecko_driver_api); \
\
static int gpio_gecko_port##pl##_init(struct device *dev) \
{ \
	gpio_gecko_add_port(&gpio_gecko_common_data, dev); \
	return 0; \
}

#ifdef CONFIG_GPIO_GECKO_PORTA
GPIO_PORT_INIT(a, A)
#endif /* CONFIG_GPIO_GECKO_PORTA */

#ifdef CONFIG_GPIO_GECKO_PORTB
GPIO_PORT_INIT(b, B)
#endif /* CONFIG_GPIO_GECKO_PORTB */

#ifdef CONFIG_GPIO_GECKO_PORTC
GPIO_PORT_INIT(c, C)
#endif /* CONFIG_GPIO_GECKO_PORTC */

#ifdef CONFIG_GPIO_GECKO_PORTD
GPIO_PORT_INIT(d, D)
#endif /* CONFIG_GPIO_GECKO_PORTD */

#ifdef CONFIG_GPIO_GECKO_PORTE
GPIO_PORT_INIT(e, E)
#endif /* CONFIG_GPIO_GECKO_PORTE */

#ifdef CONFIG_GPIO_GECKO_PORTF
GPIO_PORT_INIT(f, F)
#endif /* CONFIG_GPIO_GECKO_PORTF */

#ifdef CONFIG_GPIO_GECKO_PORTG
GPIO_PORT_INIT(g, G)
#endif /* CONFIG_GPIO_GECKO_PORTG */

#ifdef CONFIG_GPIO_GECKO_PORTH
GPIO_PORT_INIT(h, H)
#endif /* CONFIG_GPIO_GECKO_PORTH */

#ifdef CONFIG_GPIO_GECKO_PORTI
GPIO_PORT_INIT(i, I)
#endif /* CONFIG_GPIO_GECKO_PORTI */

#ifdef CONFIG_GPIO_GECKO_PORTJ
GPIO_PORT_INIT(j, J)
#endif /* CONFIG_GPIO_GECKO_PORTJ */

#ifdef CONFIG_GPIO_GECKO_PORTK
GPIO_PORT_INIT(k, K)
#endif /* CONFIG_GPIO_GECKO_PORTK */
