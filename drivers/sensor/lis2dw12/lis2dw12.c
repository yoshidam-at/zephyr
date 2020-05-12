/* ST Microelectronics LIS2DW12 3-axis accelerometer driver
 *
 * Copyright (c) 2019 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Datasheet:
 * https://www.st.com/resource/en/datasheet/lis2dw12.pdf
 */

#include <init.h>
#include <sys/__assert.h>
#include <sys/byteorder.h>
#include <logging/log.h>
#include <drivers/sensor.h>

#if defined(DT_ST_LIS2DW12_BUS_SPI)
#include <drivers/spi.h>
#elif defined(DT_ST_LIS2DW12_BUS_I2C)
#include <drivers/i2c.h>
#endif

#include "lis2dw12.h"

#define LOG_LEVEL CONFIG_SENSOR_LOG_LEVEL
LOG_MODULE_REGISTER(LIS2DW12);

/**
 * lis2dw12_set_range - set full scale range for acc
 * @dev: Pointer to instance of struct device (I2C or SPI)
 * @range: Full scale range (2, 4, 8 and 16 G)
 */
static int lis2dw12_set_range(struct device *dev, u16_t range)
{
	int err;
	struct lis2dw12_data *lis2dw12 = dev->driver_data;
	const struct lis2dw12_device_config *cfg = dev->config->config_info;
	u8_t shift_gain = 0U;
	u8_t fs = LIS2DW12_FS_TO_REG(range);

	err = lis2dw12_full_scale_set(lis2dw12->ctx, fs);

	if (cfg->pm == LIS2DW12_CONT_LOW_PWR_12bit) {
		shift_gain = LIS2DW12_SHFT_GAIN_NOLP1;
	}

	if (!err) {
		/* save internally gain for optimization */
		lis2dw12->gain =
			LIS2DW12_FS_TO_GAIN(LIS2DW12_FS_TO_REG(range),
					    shift_gain);
	}

	return err;
}

/**
 * lis2dw12_set_odr - set new sampling frequency
 * @dev: Pointer to instance of struct device (I2C or SPI)
 * @odr: Output data rate
 */
static int lis2dw12_set_odr(struct device *dev, u16_t odr)
{
	struct lis2dw12_data *lis2dw12 = dev->driver_data;
	u8_t val;

	/* check if power off */
	if (odr == 0U) {
		return lis2dw12_data_rate_set(lis2dw12->ctx,
					      LIS2DW12_XL_ODR_OFF);
	}

	val =  LIS2DW12_ODR_TO_REG(odr);
	if (val > LIS2DW12_XL_ODR_1k6Hz) {
		LOG_ERR("ODR too high");
		return -ENOTSUP;
	}

	return lis2dw12_data_rate_set(lis2dw12->ctx, val);
}

static inline void lis2dw12_convert(struct sensor_value *val, int raw_val,
				    float gain)
{
	s64_t dval;

	/* Gain is in ug/LSB */
	/* Convert to m/s^2 */
	dval = ((s64_t)raw_val * gain * SENSOR_G) / 1000000LL;
	val->val1 = dval / 1000000LL;
	val->val2 = dval % 1000000LL;
}

static inline void lis2dw12_channel_get_acc(struct device *dev,
					     enum sensor_channel chan,
					     struct sensor_value *val)
{
	int i;
	u8_t ofs_start, ofs_stop;
	struct lis2dw12_data *lis2dw12 = dev->driver_data;
	struct sensor_value *pval = val;

	switch (chan) {
	case SENSOR_CHAN_ACCEL_X:
		ofs_start = ofs_stop = 0U;
		break;
	case SENSOR_CHAN_ACCEL_Y:
		ofs_start = ofs_stop = 1U;
		break;
	case SENSOR_CHAN_ACCEL_Z:
		ofs_start = ofs_stop = 2U;
		break;
	default:
		ofs_start = 0U; ofs_stop = 2U;
		break;
	}

	for (i = ofs_start; i <= ofs_stop ; i++) {
		lis2dw12_convert(pval++, lis2dw12->acc[i], lis2dw12->gain);
	}
}

static int lis2dw12_channel_get(struct device *dev,
				 enum sensor_channel chan,
				 struct sensor_value *val)
{
	switch (chan) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_ACCEL_XYZ:
		lis2dw12_channel_get_acc(dev, chan, val);
		return 0;
	default:
		LOG_DBG("Channel not supported");
		break;
	}

	return -ENOTSUP;
}

static int lis2dw12_config(struct device *dev, enum sensor_channel chan,
			    enum sensor_attribute attr,
			    const struct sensor_value *val)
{
	switch (attr) {
	case SENSOR_ATTR_FULL_SCALE:
		return lis2dw12_set_range(dev, sensor_ms2_to_g(val));
	case SENSOR_ATTR_SAMPLING_FREQUENCY:
		return lis2dw12_set_odr(dev, val->val1);
	default:
		LOG_DBG("Acc attribute not supported");
		break;
	}

	return -ENOTSUP;
}

static int lis2dw12_attr_set(struct device *dev, enum sensor_channel chan,
			      enum sensor_attribute attr,
			      const struct sensor_value *val)
{
	switch (chan) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_ACCEL_XYZ:
		return lis2dw12_config(dev, chan, attr, val);
	default:
		LOG_DBG("Attr not supported on %d channel", chan);
		break;
	}

	return -ENOTSUP;
}

static int lis2dw12_sample_fetch(struct device *dev, enum sensor_channel chan)
{
	struct lis2dw12_data *lis2dw12 = dev->driver_data;
	const struct lis2dw12_device_config *cfg = dev->config->config_info;
	u8_t shift;
	axis3bit16_t buf;

	/* fetch raw data sample */
	if (lis2dw12_acceleration_raw_get(lis2dw12->ctx, buf.u8bit) < 0) {
		LOG_DBG("Failed to fetch raw data sample");
		return -EIO;
	}

	/* adjust to resolution */
	if (cfg->pm == LIS2DW12_CONT_LOW_PWR_12bit) {
		shift = LIS2DW12_SHIFT_PM1;
	} else {
		shift = LIS2DW12_SHIFT_PMOTHER;
	}

	lis2dw12->acc[0] = sys_le16_to_cpu(buf.i16bit[0]) >> shift;
	lis2dw12->acc[1] = sys_le16_to_cpu(buf.i16bit[1]) >> shift;
	lis2dw12->acc[2] = sys_le16_to_cpu(buf.i16bit[2]) >> shift;

	return 0;
}

static const struct sensor_driver_api lis2dw12_driver_api = {
	.attr_set = lis2dw12_attr_set,
#if CONFIG_LIS2DW12_TRIGGER
	.trigger_set = lis2dw12_trigger_set,
#endif /* CONFIG_LIS2DW12_TRIGGER */
	.sample_fetch = lis2dw12_sample_fetch,
	.channel_get = lis2dw12_channel_get,
};

static int lis2dw12_init_interface(struct device *dev)
{
	struct lis2dw12_data *lis2dw12 = dev->driver_data;
	const struct lis2dw12_device_config *cfg = dev->config->config_info;

	lis2dw12->bus = device_get_binding(cfg->bus_name);
	if (!lis2dw12->bus) {
		LOG_DBG("master bus not found: %s", cfg->bus_name);
		return -EINVAL;
	}

#if defined(DT_ST_LIS2DW12_BUS_SPI)
	lis2dw12_spi_init(dev);
#elif defined(DT_ST_LIS2DW12_BUS_I2C)
	lis2dw12_i2c_init(dev);
#else
#error "BUS MACRO NOT DEFINED IN DTS"
#endif

	return 0;
}

static int lis2dw12_set_power_mode(struct lis2dw12_data *lis2dw12,
				    lis2dw12_mode_t pm)
{
	u8_t regval = LIS2DW12_CONT_LOW_PWR_12bit;

	switch (pm) {
	case LIS2DW12_CONT_LOW_PWR_2:
	case LIS2DW12_CONT_LOW_PWR_3:
	case LIS2DW12_CONT_LOW_PWR_4:
	case LIS2DW12_HIGH_PERFORMANCE:
		regval = pm;
		break;
	default:
		LOG_DBG("Apply default Power Mode");
		break;
	}

	return lis2dw12_write_reg(lis2dw12->ctx, LIS2DW12_CTRL1, &regval, 1);
}

static int lis2dw12_init(struct device *dev)
{
	struct lis2dw12_data *lis2dw12 = dev->driver_data;
	const struct lis2dw12_device_config *cfg = dev->config->config_info;
	u8_t wai;

	if (lis2dw12_init_interface(dev)) {
		return -EINVAL;
	}

	/* check chip ID */
	if (lis2dw12_device_id_get(lis2dw12->ctx, &wai) < 0) {
		return -EIO;
	}

	if (wai != LIS2DW12_ID) {
		LOG_ERR("Invalid chip ID");
		return -EINVAL;
	}

	/* reset device */
	if (lis2dw12_reset_set(lis2dw12->ctx, PROPERTY_ENABLE) < 0) {
		return -EIO;
	}

	k_busy_wait(100);

	if (lis2dw12_block_data_update_set(lis2dw12->ctx,
					   PROPERTY_ENABLE) < 0) {
		return -EIO;
	}

	/* set power mode */
	if (lis2dw12_set_power_mode(lis2dw12, CONFIG_LIS2DW12_POWER_MODE)) {
		return -EIO;
	}

	/* set default odr and full scale for acc */
	if (lis2dw12_data_rate_set(lis2dw12->ctx, LIS2DW12_DEFAULT_ODR) < 0) {
		return -EIO;
	}

	if (lis2dw12_full_scale_set(lis2dw12->ctx, LIS2DW12_ACC_FS) < 0) {
		return -EIO;
	}

	lis2dw12->gain =
		LIS2DW12_FS_TO_GAIN(LIS2DW12_ACC_FS,
				    cfg->pm == LIS2DW12_CONT_LOW_PWR_12bit ?
				    LIS2DW12_SHFT_GAIN_NOLP1 : 0);

#ifdef CONFIG_LIS2DW12_TRIGGER
	if (lis2dw12_init_interrupt(dev) < 0) {
		LOG_ERR("Failed to initialize interrupts");
		return -EIO;
	}
#endif /* CONFIG_LIS2DW12_TRIGGER */

	return 0;
}

const struct lis2dw12_device_config lis2dw12_cfg = {
	.bus_name = DT_INST_0_ST_LIS2DW12_BUS_NAME,
	.pm = CONFIG_LIS2DW12_POWER_MODE,
#ifdef CONFIG_LIS2DW12_TRIGGER
	.int_gpio_port = DT_INST_0_ST_LIS2DW12_IRQ_GPIOS_CONTROLLER,
	.int_gpio_pin = DT_INST_0_ST_LIS2DW12_IRQ_GPIOS_PIN,
#if defined(CONFIG_LIS2DW12_INT_PIN_1)
	.int_pin = 1,
#elif defined(CONFIG_LIS2DW12_INT_PIN_2)
	.int_pin = 2,
#endif /* CONFIG_LIS2DW12_INT_PIN */

#endif /* CONFIG_LIS2DW12_TRIGGER */
};

struct lis2dw12_data lis2dw12_data;

DEVICE_AND_API_INIT(lis2dw12, DT_INST_0_ST_LIS2DW12_LABEL, lis2dw12_init,
	     &lis2dw12_data, &lis2dw12_cfg, POST_KERNEL,
	     CONFIG_SENSOR_INIT_PRIORITY, &lis2dw12_driver_api);
