/*
 * Copyright (c) 2019 Interay Solutions B.V.
 * Copyright (c) 2019 Oane Kingma
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __INC_BOARD_H
#define __INC_BOARD_H

/* This pin is used to enable the serial port using the board controller */
#define BC_ENABLE_GPIO_NAME		DT_GPIO_GECKO_PORTE_NAME
#define BC_ENABLE_GPIO_PIN		1

/* Ethernet specific pins */
#ifdef CONFIG_ETH_GECKO
#define ETH_PWR_ENABLE_GPIO_NAME	DT_GPIO_GECKO_PORTI_NAME
#define ETH_PWR_ENABLE_GPIO_PIN		10

#define ETH_RESET_GPIO_NAME		DT_GPIO_GECKO_PORTH_NAME
#define ETH_RESET_GPIO_PIN		7

#define ETH_REF_CLK_GPIO_NAME		DT_GPIO_GECKO_PORTD_NAME
#define ETH_REF_CLK_GPIO_PIN		DT_INST_0_SILABS_GECKO_ETHERNET_LOCATION_RMII_REFCLK_2
#define ETH_REF_CLK_LOCATION		DT_INST_0_SILABS_GECKO_ETHERNET_LOCATION_RMII_REFCLK_0

#endif /* CONFIG_ETH_GECKO */

#endif /* __INC_BOARD_H */
