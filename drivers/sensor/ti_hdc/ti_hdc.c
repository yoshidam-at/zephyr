/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <device.h>
#include <drivers/i2c.h>
#include <drivers/gpio.h>
#include <kernel.h>
#include <drivers/sensor.h>
#include <sys/util.h>
#include <sys/__assert.h>
#include <logging/log.h>

#include "ti_hdc.h"

#define LOG_LEVEL CONFIG_SENSOR_LOG_LEVEL
LOG_MODULE_REGISTER(TI_HDC);

#if defined(DT_INST_0_TI_HDC_DRDY_GPIOS_CONTROLLER)
static void ti_hdc_gpio_callback(struct device *dev,
				  struct gpio_callback *cb, u32_t pins)
{
	struct ti_hdc_data *drv_data =
		CONTAINER_OF(cb, struct ti_hdc_data, gpio_cb);

	ARG_UNUSED(pins);

	gpio_pin_disable_callback(dev, DT_INST_0_TI_HDC_DRDY_GPIOS_PIN);
	k_sem_give(&drv_data->data_sem);
}
#endif

static int ti_hdc_sample_fetch(struct device *dev, enum sensor_channel chan)
{
	struct ti_hdc_data *drv_data = dev->driver_data;
	u8_t buf[4];

	__ASSERT_NO_MSG(chan == SENSOR_CHAN_ALL);

#if defined(DT_INST_0_TI_HDC_DRDY_GPIOS_CONTROLLER)
	gpio_pin_enable_callback(drv_data->gpio, DT_INST_0_TI_HDC_DRDY_GPIOS_PIN);
#endif

	buf[0] = TI_HDC_REG_TEMP;
	if (i2c_write(drv_data->i2c, buf, 1,
		      DT_INST_0_TI_HDC_BASE_ADDRESS) < 0) {
		LOG_DBG("Failed to write address pointer");
		return -EIO;
	}

#if defined(DT_INST_0_TI_HDC_DRDY_GPIOS_CONTROLLER)
	k_sem_take(&drv_data->data_sem, K_FOREVER);
#else
	/* wait for the conversion to finish */
	k_sleep(HDC_CONVERSION_TIME);
#endif

	if (i2c_read(drv_data->i2c, buf, 4, DT_INST_0_TI_HDC_BASE_ADDRESS) < 0) {
		LOG_DBG("Failed to read sample data");
		return -EIO;
	}

	drv_data->t_sample = (buf[0] << 8) + buf[1];
	drv_data->rh_sample = (buf[2] << 8) + buf[3];

	return 0;
}


static int ti_hdc_channel_get(struct device *dev,
			      enum sensor_channel chan,
			      struct sensor_value *val)
{
	struct ti_hdc_data *drv_data = dev->driver_data;
	u64_t tmp;

	/*
	 * See datasheet "Temperature Register" and "Humidity
	 * Register" sections for more details on processing
	 * sample data.
	 */
	if (chan == SENSOR_CHAN_AMBIENT_TEMP) {
		/* val = -40 + 165 * sample / 2^16 */
		tmp = (u64_t)drv_data->t_sample * 165U;
		val->val1 = (s32_t)(tmp >> 16) - 40;
		val->val2 = ((tmp & 0xFFFF) * 1000000U) >> 16;
	} else if (chan == SENSOR_CHAN_HUMIDITY) {
		/* val = 100 * sample / 2^16 */
		tmp = (u64_t)drv_data->rh_sample * 100U;
		val->val1 = tmp >> 16;
		/* x * 1000000 / 65536 == x * 15625 / 1024 */
		val->val2 = ((tmp & 0xFFFF) * 15625U) >> 10;
	} else {
		return -ENOTSUP;
	}

	return 0;
}

static const struct sensor_driver_api ti_hdc_driver_api = {
	.sample_fetch = ti_hdc_sample_fetch,
	.channel_get = ti_hdc_channel_get,
};

static u16_t read16(struct device *dev, u8_t a, u8_t d)
{
	u8_t buf[2];
	if (i2c_burst_read(dev, a, d, (u8_t *)buf, 2) < 0) {
		LOG_ERR("Error reading register.");
	}
	return (buf[0] << 8 | buf[1]);
}

static int ti_hdc_init(struct device *dev)
{
	struct ti_hdc_data *drv_data = dev->driver_data;
	u16_t tmp;

	drv_data->i2c = device_get_binding(DT_INST_0_TI_HDC_BUS_NAME);

	if (drv_data->i2c == NULL) {
		LOG_DBG("Failed to get pointer to %s device!",
			DT_INST_0_TI_HDC_BUS_NAME);
		return -EINVAL;
	}

	if (read16(drv_data->i2c, DT_INST_0_TI_HDC_BASE_ADDRESS,
		   TI_HDC_REG_MANUFID) != TI_HDC_MANUFID) {
		LOG_ERR("Failed to get correct manufacturer ID");
		return -EINVAL;
	}
	tmp = read16(drv_data->i2c, DT_INST_0_TI_HDC_BASE_ADDRESS,
		     TI_HDC_REG_DEVICEID);
	if (tmp != TI_HDC1000_DEVID && tmp != TI_HDC1050_DEVID) {
		LOG_ERR("Unsupported device ID");
		return -EINVAL;
	}

#if defined(DT_INST_0_TI_HDC_DRDY_GPIOS_CONTROLLER)
	k_sem_init(&drv_data->data_sem, 0, UINT_MAX);

	/* setup data ready gpio interrupt */
	drv_data->gpio = device_get_binding(
				DT_INST_0_TI_HDC_DRDY_GPIOS_CONTROLLER);
	if (drv_data->gpio == NULL) {
		LOG_DBG("Failed to get pointer to %s device",
			 DT_INST_0_TI_HDC_DRDY_GPIOS_CONTROLLER);
		return -EINVAL;
	}

	gpio_pin_configure(drv_data->gpio, DT_INST_0_TI_HDC_DRDY_GPIOS_PIN,
			   GPIO_DIR_IN | GPIO_INT | GPIO_INT_EDGE |
#if defined(DT_INST_0_TI_HDC_DRDY_GPIOS_FLAGS)
			   DT_INST_0_TI_HDC_DRDY_GPIOS_FLAGS |
#endif
			   GPIO_INT_ACTIVE_LOW | GPIO_INT_DEBOUNCE);

	gpio_init_callback(&drv_data->gpio_cb,
			   ti_hdc_gpio_callback,
			   BIT(DT_INST_0_TI_HDC_DRDY_GPIOS_PIN));

	if (gpio_add_callback(drv_data->gpio, &drv_data->gpio_cb) < 0) {
		LOG_DBG("Failed to set GPIO callback");
		return -EIO;
	}
#endif

	LOG_INF("Initialized device successfully");

	return 0;
}

static struct ti_hdc_data ti_hdc_data;

DEVICE_AND_API_INIT(ti_hdc, DT_INST_0_TI_HDC_LABEL, ti_hdc_init, &ti_hdc_data,
		    NULL, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,
		    &ti_hdc_driver_api);
