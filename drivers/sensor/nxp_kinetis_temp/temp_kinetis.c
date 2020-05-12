/*
 * Copyright (c) 2020 Vestas Wind Systems A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <device.h>
#include <drivers/sensor.h>
#include <drivers/adc.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(temp_kinetis, CONFIG_SENSOR_LOG_LEVEL);

/*
 * Driver assumptions:
 * - ADC samples are in u16_t format
 * - Both ADC channels (sensor and bandgap) are on the same ADC instance
 *
 * See NXP Application Note AN3031 for details on calculations.
 */

/* Two ADC samples required for each reading, sensor value and bandgap value */
#define TEMP_KINETIS_ADC_SAMPLES 2

struct temp_kinetis_config {
	const char *adc_dev_name;
	u8_t sensor_adc_ch;
	u8_t bandgap_adc_ch;
	int bandgap_mv;
	int vtemp25_mv;
	int slope_cold_uv;
	int slope_hot_uv;
	struct adc_sequence adc_seq;
};

struct temp_kinetis_data {
	struct device *adc;
	u16_t buffer[TEMP_KINETIS_ADC_SAMPLES];
};

static int temp_kinetis_sample_fetch(struct device *dev,
				     enum sensor_channel chan)
{
	const struct temp_kinetis_config *config = dev->config->config_info;
	struct temp_kinetis_data *data = dev->driver_data;
	int err;

	/* Always read both sensor and bandgap voltage in one go */
	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_DIE_TEMP &&
	    chan != SENSOR_CHAN_VOLTAGE) {
		return -ENOTSUP;
	}

	err = adc_read(data->adc, &config->adc_seq);
	if (err) {
		LOG_ERR("failed to read ADC channels (err %d)", err);
		return err;
	}

	LOG_DBG("sensor = %d, bandgap = %d", data->buffer[0], data->buffer[1]);

	return 0;
}

static int temp_kinetis_channel_get(struct device *dev,
				    enum sensor_channel chan,
				    struct sensor_value *val)
{
	const struct temp_kinetis_config *config = dev->config->config_info;
	struct temp_kinetis_data *data = dev->driver_data;
	u16_t adcr_vdd = BIT_MASK(config->adc_seq.resolution);
	u16_t adcr_temp25;
	s32_t temp_mc;
	s32_t vdd_mv;
	int slope_uv;
	u16_t m;

	if (chan != SENSOR_CHAN_VOLTAGE && chan != SENSOR_CHAN_DIE_TEMP) {
		return -ENOTSUP;
	}

	/* VDD (or VREF, but AN3031 calls it VDD) in millivolts */
	vdd_mv = (adcr_vdd * config->bandgap_mv) / data->buffer[1];

	if (chan == SENSOR_CHAN_VOLTAGE) {
		val->val1 = vdd_mv / 1000;
		val->val2 = (vdd_mv % 1000) * 1000;
		return 0;
	}

	/* ADC result for temperature = 25 degrees Celsius */
	adcr_temp25 = (adcr_vdd * config->vtemp25_mv) / vdd_mv;

	/* Determine which slope to use */
	if (data->buffer[0] > adcr_temp25) {
		slope_uv = config->slope_cold_uv;
	} else {
		slope_uv = config->slope_hot_uv;
	}

	/* m x 1000 */
	m = (adcr_vdd * slope_uv) / vdd_mv;

	/* Temperature in milli degrees Celsius */
	temp_mc = 25000 - ((data->buffer[0] - adcr_temp25) * 1000000) / m;

	val->val1 = temp_mc / 1000;
	val->val2 = (temp_mc % 1000) * 1000;

	return 0;
}

static const struct sensor_driver_api temp_kinetis_driver_api = {
	.sample_fetch = temp_kinetis_sample_fetch,
	.channel_get = temp_kinetis_channel_get,
};

static int temp_kinetis_init(struct device *dev)
{
	const struct temp_kinetis_config *config = dev->config->config_info;
	struct temp_kinetis_data *data = dev->driver_data;
	int err;
	int i;
	const struct adc_channel_cfg ch_cfg[] = {
		{
			.gain = ADC_GAIN_1,
			.reference = ADC_REF_INTERNAL,
			.acquisition_time = ADC_ACQ_TIME_DEFAULT,
			.channel_id = config->sensor_adc_ch,
			.differential = 0,
		},
		{
			.gain = ADC_GAIN_1,
			.reference = ADC_REF_INTERNAL,
			.acquisition_time = ADC_ACQ_TIME_DEFAULT,
			.channel_id = config->bandgap_adc_ch,
			.differential = 0,
		},
	};

	memset(&data->buffer, 0, ARRAY_SIZE(data->buffer));

	data->adc = device_get_binding(config->adc_dev_name);
	if (!data->adc) {
		LOG_ERR("could not get ADC device");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(ch_cfg); i++) {
		err = adc_channel_setup(data->adc, &ch_cfg[i]);
		if (err) {
			LOG_ERR("failed to configure ADC channel (err %d)",
				err);
			return err;
		}
	}

	return 0;
}

#ifdef DT_INST_0_NXP_KINETIS_TEMPERATURE
BUILD_ASSERT_MSG(DT_INST_0_NXP_KINETIS_TEMPERATURE_SENSOR_IO_CHANNELS_INPUT <
		 DT_INST_0_NXP_KINETIS_TEMPERATURE_BANDGAP_IO_CHANNELS_INPUT,
		 "This driver assumes sensor ADC channel to come before "
		 "bandgap ADC channel");

static struct temp_kinetis_data temp_kinetis_data_0;

static const struct temp_kinetis_config temp_kinetis_config_0 = {
	.adc_dev_name =
		DT_INST_0_NXP_KINETIS_TEMPERATURE_IO_CHANNELS_CONTROLLER_0,
	.sensor_adc_ch =
		DT_INST_0_NXP_KINETIS_TEMPERATURE_SENSOR_IO_CHANNELS_INPUT,
	.bandgap_adc_ch =
		DT_INST_0_NXP_KINETIS_TEMPERATURE_BANDGAP_IO_CHANNELS_INPUT,
	.bandgap_mv = DT_INST_0_NXP_KINETIS_TEMPERATURE_BANDGAP_VOLTAGE / 1000,
	.vtemp25_mv = DT_INST_0_NXP_KINETIS_TEMPERATURE_VTEMP25 / 1000,
	.slope_cold_uv = DT_INST_0_NXP_KINETIS_TEMPERATURE_SENSOR_SLOPE_COLD,
	.slope_hot_uv = DT_INST_0_NXP_KINETIS_TEMPERATURE_SENSOR_SLOPE_HOT,
	.adc_seq = {
		.options = NULL,
		.channels =
	BIT(DT_INST_0_NXP_KINETIS_TEMPERATURE_SENSOR_IO_CHANNELS_INPUT) |
	BIT(DT_INST_0_NXP_KINETIS_TEMPERATURE_BANDGAP_IO_CHANNELS_INPUT),
		.buffer = &temp_kinetis_data_0.buffer,
		.buffer_size = sizeof(temp_kinetis_data_0.buffer),
		.resolution = CONFIG_TEMP_KINETIS_RESOLUTION,
		.oversampling = CONFIG_TEMP_KINETIS_OVERSAMPLING,
		.calibrate = false,
	},
};

DEVICE_AND_API_INIT(temp_kinetis, DT_INST_0_NXP_KINETIS_TEMPERATURE_LABEL,
		    temp_kinetis_init, &temp_kinetis_data_0,
		    &temp_kinetis_config_0, POST_KERNEL,
		    CONFIG_SENSOR_INIT_PRIORITY,
		    &temp_kinetis_driver_api);

#endif /* DT_INST_0_NXP_KINETIS_TEMPERATURE */
