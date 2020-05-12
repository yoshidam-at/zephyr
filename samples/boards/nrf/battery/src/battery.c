/*
 * Copyright (c) 2018-2019 Peter Bigot Consulting, LLC
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr.h>
#include <init.h>
#include <drivers/gpio.h>
#include <drivers/adc.h>
#include <drivers/sensor.h>
#include <logging/log.h>

#include "battery.h"

LOG_MODULE_REGISTER(BATTERY, CONFIG_ADC_LOG_LEVEL);

#ifdef CONFIG_BOARD_NRF52_PCA20020
/* This board uses a divider that reduces max voltage to
 * reference voltage (600 mV).
 */
#define BATTERY_ADC_GAIN ADC_GAIN_1
#else
/* Other boards may use dividers that only reduce battery voltage to
 * the maximum supported by the hardware (3.6 V)
 */
#define BATTERY_ADC_GAIN ADC_GAIN_1_6
#endif

struct io_channel_config {
	const char *label;
	u8_t channel;
};

struct gpio_channel_config {
	const char *label;
	u8_t pin;
	u8_t flags;
};

struct divider_config {
	const struct io_channel_config io_channel;
	const struct gpio_channel_config power_gpios;
	const u32_t output_ohm;
	const u32_t full_ohm;
};

static const struct divider_config divider_config = {
	.io_channel = DT_VOLTAGE_DIVIDER_VBATT_IO_CHANNELS,
#ifdef DT_VOLTAGE_DIVIDER_VBATT_POWER_GPIOS
	.power_gpios = DT_VOLTAGE_DIVIDER_VBATT_POWER_GPIOS,
#endif
	.output_ohm = DT_VOLTAGE_DIVIDER_VBATT_OUTPUT_OHMS,
	.full_ohm = DT_VOLTAGE_DIVIDER_VBATT_FULL_OHMS,
};

struct divider_data {
	struct device *adc;
	struct device *gpio;
	struct adc_channel_cfg adc_cfg;
	struct adc_sequence adc_seq;
	s16_t raw;
};
static struct divider_data divider_data;

static int divider_setup(void)
{
	const struct divider_config *cfg = &divider_config;
	const struct io_channel_config *iocp = &cfg->io_channel;
	const struct gpio_channel_config *gcp = &cfg->power_gpios;
	struct divider_data *ddp = &divider_data;
	struct adc_sequence *asp = &ddp->adc_seq;
	struct adc_channel_cfg *accp = &ddp->adc_cfg;
	int rc;

	ddp->adc = device_get_binding(iocp->label);
	if (ddp->adc == NULL) {
		LOG_ERR("Failed to get ADC %s", iocp->label);
		return -ENOENT;
	}

	if (gcp->label) {
		ddp->gpio = device_get_binding(gcp->label);
		if (ddp->gpio == NULL) {
			LOG_ERR("Failed to get GPIO %s", gcp->label);
			return -ENOENT;
		}
		rc = gpio_pin_configure(ddp->gpio, gcp->pin,
					GPIO_OUTPUT_INACTIVE | gcp->flags);
		if (rc != 0) {
			LOG_ERR("Failed to control feed %s.%u: %d",
				gcp->label, gcp->pin, rc);
			return rc;
		}
	}

	*asp = (struct adc_sequence){
		.channels = BIT(0),
		.buffer = &ddp->raw,
		.buffer_size = sizeof(ddp->raw),
		.oversampling = 4,
		.calibrate = true,
	};

#ifdef CONFIG_ADC_NRFX_SAADC
	*accp = (struct adc_channel_cfg){
		.gain = BATTERY_ADC_GAIN,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
		.input_positive = SAADC_CH_PSELP_PSELP_AnalogInput0 + iocp->channel,
	};

	asp->resolution = 14;
#else /* CONFIG_ADC_var */
#error Unsupported ADC
#endif /* CONFIG_ADC_var */

	rc = adc_channel_setup(ddp->adc, accp);
	LOG_INF("Setup AIN%u got %d", iocp->channel, rc);

	return rc;
}

static bool battery_ok;

static int battery_setup(struct device *arg)
{
	int rc = divider_setup();

	battery_ok = (rc == 0);
	LOG_INF("Battery setup: %d %d", rc, battery_ok);
	return rc;
}

SYS_INIT(battery_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

int battery_measure_enable(bool enable)
{
	int rc = -ENOENT;

	if (battery_ok) {
		const struct divider_data *ddp = &divider_data;
		const struct gpio_channel_config *gcp = &divider_config.power_gpios;

		rc = 0;
		if (ddp->gpio) {
			rc = gpio_pin_set(ddp->gpio, gcp->pin, enable);
		}
	}
	return rc;
}

int battery_sample(void)
{
	int rc = -ENOENT;

	if (battery_ok) {
		struct divider_data *ddp = &divider_data;
		const struct divider_config *dcp = &divider_config;
		struct adc_sequence *sp = &ddp->adc_seq;

		rc = adc_read(ddp->adc, sp);
		sp->calibrate = false;
		if (rc == 0) {
			s32_t val = ddp->raw;

			adc_raw_to_millivolts(adc_ref_internal(ddp->adc),
					      ddp->adc_cfg.gain,
					      sp->resolution,
					      &val);
			rc = val * (u64_t)dcp->full_ohm / dcp->output_ohm;
			LOG_INF("raw %u ~ %u mV => %d mV\n",
				ddp->raw, val, rc);
		}
	}

	return rc;
}

unsigned int battery_level_pptt(unsigned int batt_mV,
				const struct battery_level_point *curve)
{
	const struct battery_level_point *pb = curve;

	if (batt_mV >= pb->lvl_mV) {
		/* Measured voltage above highest point, cap at maximum. */
		return pb->lvl_pptt;
	}
	/* Go down to the last point at or below the measured voltage. */
	while ((pb->lvl_pptt > 0)
	       && (batt_mV < pb->lvl_mV)) {
		++pb;
	}
	if (batt_mV < pb->lvl_mV) {
		/* Below lowest point, cap at minimum */
		return pb->lvl_pptt;
	}

	/* Linear interpolation between below and above points. */
	const struct battery_level_point *pa = pb - 1;

	return pb->lvl_pptt
	       + ((pa->lvl_pptt - pb->lvl_pptt)
		  * (batt_mV - pb->lvl_mV)
		  / (pa->lvl_mV - pb->lvl_mV));
}
