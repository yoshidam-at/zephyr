/*
 * Copyright (c) 2015-2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __TEST_GPIO_H__
#define __TEST_GPIO_H__

#include <zephyr.h>
#include <drivers/gpio.h>
#include <sys/util.h>
#include <ztest.h>

#ifdef DT_INST_0_TEST_GPIO_BASIC_API

/* Execution of the test requires hardware configuration described in
 * devicetree.  See the test,gpio_basic_api binding local to this test
 * for details.
 *
 * If this is not present devices that have gpio-0, gpio-1, or gpio-2
 * aliases are supported for build-only tests.
 */
#define DEV_NAME DT_INST_0_TEST_GPIO_BASIC_API_OUT_GPIOS_CONTROLLER
#define PIN_OUT DT_INST_0_TEST_GPIO_BASIC_API_OUT_GPIOS_PIN
#define PIN_IN DT_INST_0_TEST_GPIO_BASIC_API_IN_GPIOS_PIN

#elif defined(DT_ALIAS_GPIO_0_LABEL)
#define DEV_NAME DT_ALIAS_GPIO_0_LABEL
#elif defined(DT_ALIAS_GPIO_1_LABEL)
#define DEV_NAME DT_ALIAS_GPIO_1_LABEL
#elif defined(DT_ALIAS_GPIO_3_LABEL)
#define DEV_NAME DT_ALIAS_GPIO_3_LABEL
#else
#error Unsupported board
#endif

#ifndef PIN_OUT
/* For build-only testing use fixed pins. */
#define PIN_OUT 2
#define PIN_IN 3
#endif

#define MAX_INT_CNT 3
struct drv_data {
	struct gpio_callback gpio_cb;
	gpio_flags_t mode;
	int index;
	int aux;
};

void test_gpio_pin_read_write(void);
void test_gpio_callback_add_remove(void);
void test_gpio_callback_self_remove(void);
void test_gpio_callback_enable_disable(void);
void test_gpio_callback_variants(void);

void test_gpio_port(void);

void test_gpio_deprecated(void);

#endif /* __TEST_GPIO_H__ */
