/*
 * Copyright (c) 2019 Vestas Wind Systems A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_ADC_LMP90XXX_H_
#define ZEPHYR_INCLUDE_DRIVERS_ADC_LMP90XXX_H_

#include <device.h>
#include <drivers/gpio.h>

/* LMP90xxx supports GPIO D0..D6 */
#define LMP90XXX_GPIO_MAX 6

int lmp90xxx_gpio_set_output(struct device *dev, u8_t pin);

int lmp90xxx_gpio_set_input(struct device *dev, u8_t pin);

int lmp90xxx_gpio_set_pin_value(struct device *dev, u8_t pin, bool value);

int lmp90xxx_gpio_get_pin_value(struct device *dev, u8_t pin, bool *value);

int lmp90xxx_gpio_port_get_raw(struct device *dev, gpio_port_value_t *value);

int lmp90xxx_gpio_port_set_masked_raw(struct device *dev,
				      gpio_port_pins_t mask,
				      gpio_port_value_t value);

int lmp90xxx_gpio_port_set_bits_raw(struct device *dev, gpio_port_pins_t pins);

int lmp90xxx_gpio_port_clear_bits_raw(struct device *dev,
				      gpio_port_pins_t pins);

int lmp90xxx_gpio_port_toggle_bits(struct device *dev, gpio_port_pins_t pins);

#endif /* ZEPHYR_INCLUDE_DRIVERS_ADC_LMP90XXX_H_ */
