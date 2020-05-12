/*
 * Copyright (c) 2018 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/sensor.h>
#include "ccs811.h"

#define LOG_LEVEL CONFIG_SENSOR_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_DECLARE(CCS811);

#define IRQ_PIN DT_INST_0_AMS_CCS811_IRQ_GPIOS_PIN

int ccs811_attr_set(struct device *dev,
		    enum sensor_channel chan,
		    enum sensor_attribute attr,
		    const struct sensor_value *thr)
{
	struct ccs811_data *drv_data = dev->driver_data;
	int rc;

	if (chan != SENSOR_CHAN_CO2) {
		rc = -ENOTSUP;
	} else if (attr == SENSOR_ATTR_LOWER_THRESH) {
		rc = -EINVAL;
		if ((thr->val1 >= CCS811_CO2_MIN_PPM)
		    && (thr->val1 <= CCS811_CO2_MAX_PPM)) {
			drv_data->co2_l2m = thr->val1;
			rc = 0;
		}
	} else if (attr == SENSOR_ATTR_UPPER_THRESH) {
		rc = -EINVAL;
		if ((thr->val1 >= CCS811_CO2_MIN_PPM)
		    && (thr->val1 <= CCS811_CO2_MAX_PPM)) {
			drv_data->co2_m2h = thr->val1;
			rc = 0;
		}
	} else {
		rc = -ENOTSUP;
	}
	return rc;
}

static inline void setup_irq(struct device *dev,
			     bool enable)
{
	struct ccs811_data *data = dev->driver_data;
	unsigned int flags = enable
			     ? GPIO_INT_LEVEL_ACTIVE
			     : GPIO_INT_DISABLE;

	gpio_pin_interrupt_configure(data->irq_gpio, IRQ_PIN, flags);
}

static inline void handle_irq(struct device *dev)
{
	struct ccs811_data *data = dev->driver_data;

	setup_irq(dev, false);

#if defined(CONFIG_CCS811_TRIGGER_OWN_THREAD)
	k_sem_give(&data->gpio_sem);
#elif defined(CONFIG_CCS811_TRIGGER_GLOBAL_THREAD)
	k_work_submit(&data->work);
#endif
}

static void process_irq(struct device *dev)
{
	struct ccs811_data *data = dev->driver_data;

	if (data->handler != NULL) {
		data->handler(dev, &data->trigger);
	}

	if (data->handler != NULL) {
		setup_irq(dev, true);
	}
}

static void gpio_callback(struct device *dev,
			  struct gpio_callback *cb,
			  u32_t pins)
{
	struct ccs811_data *data =
		CONTAINER_OF(cb, struct ccs811_data, gpio_cb);

	ARG_UNUSED(pins);

	handle_irq(data->dev);
}

#ifdef CONFIG_CCS811_TRIGGER_OWN_THREAD
static void irq_thread(int dev_ptr, int unused)
{
	struct device *dev = INT_TO_POINTER(dev_ptr);
	struct ccs811_data *drv_data = dev->driver_data;

	ARG_UNUSED(unused);

	while (1) {
		k_sem_take(&drv_data->gpio_sem, K_FOREVER);
		process_irq(dev);
	}
}
#elif defined(CONFIG_CCS811_TRIGGER_GLOBAL_THREAD)
static void work_cb(struct k_work *work)
{
	struct ccs811_data *data = CONTAINER_OF(work, struct ccs811_data, work);

	process_irq(data->dev);
}
#else
#error Unhandled trigger configuration
#endif

int ccs811_trigger_set(struct device *dev,
		       const struct sensor_trigger *trig,
		       sensor_trigger_handler_t handler)
{
	struct ccs811_data *drv_data = dev->driver_data;
	u8_t drdy_thresh = CCS811_MODE_THRESH | CCS811_MODE_DATARDY;
	int rc;

	LOG_DBG("CCS811 trigger set");
	setup_irq(dev, false);

	drv_data->handler = handler;
	if (handler == NULL) {
		return 0;
	}

	if (trig->type == SENSOR_TRIG_DATA_READY) {
		rc = ccs811_mutate_meas_mode(dev, CCS811_MODE_DATARDY,
					     CCS811_MODE_THRESH);
	} else if (trig->type == SENSOR_TRIG_THRESHOLD) {
		rc = -EINVAL;
		if ((drv_data->co2_l2m >= CCS811_CO2_MIN_PPM)
		    && (drv_data->co2_l2m <= CCS811_CO2_MAX_PPM)
		    && (drv_data->co2_m2h >= CCS811_CO2_MIN_PPM)
		    && (drv_data->co2_m2h <= CCS811_CO2_MAX_PPM)
		    && (drv_data->co2_l2m <= drv_data->co2_m2h)) {
			rc = ccs811_set_thresholds(dev);
		}
		if (rc == 0) {
			rc = ccs811_mutate_meas_mode(dev, drdy_thresh, 0);
		}
	} else {
		rc = -ENOTSUP;
	}

	if (rc == 0) {
		drv_data->trigger = *trig;
		setup_irq(dev, true);

		if (gpio_pin_get(drv_data->irq_gpio, IRQ_PIN) > 0) {
			handle_irq(dev);
		}
	} else {
		drv_data->handler = NULL;
		(void)ccs811_mutate_meas_mode(dev, 0, drdy_thresh);
	}

	return rc;
}

int ccs811_init_interrupt(struct device *dev)
{
	struct ccs811_data *drv_data = dev->driver_data;

	drv_data->dev = dev;

	gpio_pin_configure(drv_data->irq_gpio, IRQ_PIN,
			   GPIO_INPUT | DT_INST_0_AMS_CCS811_IRQ_GPIOS_FLAGS);

	gpio_init_callback(&drv_data->gpio_cb, gpio_callback, BIT(IRQ_PIN));

	if (gpio_add_callback(drv_data->irq_gpio, &drv_data->gpio_cb) < 0) {
		LOG_DBG("Failed to set gpio callback!");
		return -EIO;
	}

#if defined(CONFIG_CCS811_TRIGGER_OWN_THREAD)
	k_sem_init(&drv_data->gpio_sem, 0, UINT_MAX);

	k_thread_create(&drv_data->thread, drv_data->thread_stack,
			CONFIG_CCS811_THREAD_STACK_SIZE,
			(k_thread_entry_t)irq_thread, dev,
			0, NULL, K_PRIO_COOP(CONFIG_CCS811_THREAD_PRIORITY),
			0, 0);
#elif defined(CONFIG_CCS811_TRIGGER_GLOBAL_THREAD)
	drv_data->work.handler = work_cb;
#else
#error Unhandled trigger configuration
#endif
	return 0;
}
