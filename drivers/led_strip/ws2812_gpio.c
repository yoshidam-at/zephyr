/*
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/led_strip.h>

#include <string.h>

#define LOG_LEVEL CONFIG_LED_STRIP_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(ws2812_gpio);

#include <zephyr.h>
#include <soc.h>
#include <drivers/gpio.h>
#include <device.h>
#include <drivers/clock_control.h>
#include <drivers/clock_control/nrf_clock_control.h>

struct ws2812_gpio_data {
	struct device *gpio;
	struct device *clk;
};

struct ws2812_gpio_cfg {
	u8_t pin;
	bool has_white;
};

static struct ws2812_gpio_data *dev_data(struct device *dev)
{
	return dev->driver_data;
}

static const struct ws2812_gpio_cfg *dev_cfg(struct device *dev)
{
	return dev->config->config_info;
}

/*
 * This is hard-coded to nRF51 in two ways:
 *
 * 1. The assembly delays T1H, T0H, TxL
 * 2. GPIO set/clear
 */

/*
 * T1H: 1 bit high pulse delay: 12 cycles == .75 usec
 * T0H: 0 bit high pulse delay: 4 cycles == .25 usec
 * TxL: inter-bit low pulse delay: 8 cycles == .5 usec
 *
 * We can't use k_busy_wait() here: its argument is in microseconds,
 * and we need roughly .05 microsecond resolution.
 */
#define DELAY_T1H "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
#define DELAY_T0H "nop\nnop\nnop\nnop\n"
#define DELAY_TxL "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"

/*
 * GPIO set/clear (these make assumptions about assembly details
 * below).
 *
 * This uses OUTCLR == OUTSET+4.
 *
 * We should be able to make this portable using the results of
 * https://github.com/zephyrproject-rtos/zephyr/issues/11917.
 *
 * We already have the GPIO device stashed in ws2812_gpio_data, so
 * this driver can be used as a test case for the optimized API.
 *
 * Per Arm docs, both Rd and Rn must be r0-r7, so we use the "l"
 * constraint in the below assembly.
 */
#define SET_HIGH "str %[p], [%[r], #0]\n" /* OUTSET = BIT(LED_PIN) */
#define SET_LOW "str %[p], [%[r], #4]\n"  /* OUTCLR = BIT(LED_PIN) */

/* Send out a 1 bit's pulse */
#define ONE_BIT(base, pin) do {			\
	__asm volatile (SET_HIGH			\
			DELAY_T1H			\
			SET_LOW			\
			DELAY_TxL			\
			::				\
			[r] "l" (base),		\
			[p] "l" (pin)); } while (0)

/* Send out a 0 bit's pulse */
#define ZERO_BIT(base, pin) do {			\
	__asm volatile (SET_HIGH			\
			DELAY_T0H			\
			SET_LOW			\
			DELAY_TxL			\
			::				\
			[r] "l" (base),		\
			[p] "l" (pin)); } while (0)

static int send_buf(struct device *dev, u8_t *buf, size_t len)
{
	volatile u32_t *base = (u32_t *)&NRF_GPIO->OUTSET;
	const u32_t val = BIT(dev_cfg(dev)->pin);
	struct device *clk = dev_data(dev)->clk;
	unsigned int key;
	int rc;

	rc = clock_control_on(clk, CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (rc) {
		return rc;
	}

	key = irq_lock();

	while (len--) {
		u32_t b = *buf++;
		s32_t i;

		/*
		 * Generate signal out of the bits, MSbit first.
		 *
		 * Accumulator maintenance and branching mean the
		 * inter-bit time will be longer than TxL, but the
		 * wp.josh.com blog post says we have at least 5 usec
		 * of slack time between bits before we risk the
		 * signal getting latched, so this will be fine as
		 * long as the compiler does something minimally
		 * reasonable.
		 */
		for (i = 7; i >= 0; i--) {
			if (b & BIT(i)) {
				ONE_BIT(base, val);
			} else {
				ZERO_BIT(base, val);
			}
		}
	}

	irq_unlock(key);

	rc = clock_control_off(clk, CLOCK_CONTROL_NRF_SUBSYS_HF);

	return rc;
}

static int ws2812_gpio_update_rgb(struct device *dev, struct led_rgb *pixels,
				  size_t num_pixels)
{
	const struct ws2812_gpio_cfg *config = dev->config->config_info;
	const bool has_white = config->has_white;
	u8_t *ptr = (u8_t *)pixels;
	size_t i;

	/* Convert from RGB to on-wire format (GRB or GRBW) */
	for (i = 0; i < num_pixels; i++) {
		u8_t r = pixels[i].r;
		u8_t g = pixels[i].g;
		u8_t b = pixels[i].b;

		*ptr++ = g;
		*ptr++ = r;
		*ptr++ = b;
		if (has_white) {
			*ptr++ = 0; /* white channel is unused */
		}
	}

	return send_buf(dev, (u8_t *)pixels, num_pixels * (has_white ? 4 : 3));
}

static int ws2812_gpio_update_channels(struct device *dev, u8_t *channels,
				       size_t num_channels)
{
	LOG_ERR("update_channels not implemented");
	return -ENOTSUP;
}

static const struct led_strip_driver_api ws2812_gpio_api = {
	.update_rgb = ws2812_gpio_update_rgb,
	.update_channels = ws2812_gpio_update_channels,
};

#define WS2812_GPIO_LABEL(idx) \
	(DT_INST_##idx##_WORLDSEMI_WS2812_GPIO_LABEL)
#define WS2812_GPIO_HAS_WHITE(idx) \
	(DT_INST_##idx##_WORLDSEMI_WS2812_GPIO_HAS_WHITE_CHANNEL == 1)
#define WS2812_GPIO_DEV(idx) \
	(DT_INST_##idx##_WORLDSEMI_WS2812_GPIO_IN_GPIOS_CONTROLLER)
#define WS2812_GPIO_PIN(idx) \
	(DT_INST_##idx##_WORLDSEMI_WS2812_GPIO_IN_GPIOS_PIN)
#define WS2812_GPIO_FLAGS(idx) \
	(DT_INST_##idx##_WORLDSEMI_WS2812_GPIO_IN_GPIOS_FLAGS)
/*
 * The inline assembly above is designed to work on nRF51 devices with
 * the 16 MHz clock enabled.
 *
 * TODO: try to make this portable, or at least port to more devices.
 */
#define WS2812_GPIO_CLK(idx) DT_INST_0_NORDIC_NRF_CLOCK_LABEL

#define WS2812_GPIO_DEVICE(idx)					\
									\
	static int ws2812_gpio_##idx##_init(struct device *dev)	\
	{								\
		struct ws2812_gpio_data *data = dev_data(dev);		\
									\
		data->gpio = device_get_binding(WS2812_GPIO_DEV(idx));	\
		if (!data->gpio) {					\
			LOG_ERR("Unable to find GPIO controller %s",	\
				WS2812_GPIO_DEV(idx));			\
			return -ENODEV;				\
		}							\
									\
		data->clk = device_get_binding(WS2812_GPIO_CLK(idx));	\
		if (!data->clk) {					\
			LOG_ERR("Unable to find clock %s",		\
				WS2812_GPIO_CLK(idx));			\
			return -ENODEV;				\
		}							\
									\
		return gpio_pin_configure(data->gpio,			\
					  WS2812_GPIO_PIN(idx),	\
					  WS2812_GPIO_FLAGS(idx) |	\
					  GPIO_OUTPUT);		\
	}								\
									\
	static struct ws2812_gpio_data ws2812_gpio_##idx##_data;	\
									\
	static const struct ws2812_gpio_cfg ws2812_gpio_##idx##_cfg = { \
		.pin = WS2812_GPIO_PIN(idx),				\
		.has_white = WS2812_GPIO_HAS_WHITE(idx),		\
	};								\
									\
	DEVICE_AND_API_INIT(ws2812_gpio_##idx, WS2812_GPIO_LABEL(idx),	\
			    ws2812_gpio_##idx##_init,			\
			    &ws2812_gpio_##idx##_data,			\
			    &ws2812_gpio_##idx##_cfg, POST_KERNEL,	\
			    CONFIG_LED_STRIP_INIT_PRIORITY,		\
			    &ws2812_gpio_api)

#ifdef DT_INST_0_WORLDSEMI_WS2812_GPIO_LABEL
WS2812_GPIO_DEVICE(0);
#endif

#ifdef DT_INST_1_WORLDSEMI_WS2812_GPIO_LABEL
WS2812_GPIO_DEVICE(1);
#endif
