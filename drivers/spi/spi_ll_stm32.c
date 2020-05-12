/*
 * Copyright (c) 2016 BayLibre, SAS
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_LEVEL CONFIG_SPI_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(spi_ll_stm32);

#include <sys/util.h>
#include <kernel.h>
#include <soc.h>
#include <errno.h>
#include <drivers/spi.h>
#include <toolchain.h>

#include <drivers/clock_control/stm32_clock_control.h>
#include <drivers/clock_control.h>

#include "spi_ll_stm32.h"

#define DEV_CFG(dev)						\
(const struct spi_stm32_config * const)(dev->config->config_info)

#define DEV_DATA(dev)					\
(struct spi_stm32_data * const)(dev->driver_data)

/*
 * Check for SPI_SR_FRE to determine support for TI mode frame format
 * error flag, because STM32F1 SoCs do not support it and  STM32CUBE
 * for F1 family defines an unused LL_SPI_SR_FRE.
 */
#ifdef CONFIG_SOC_SERIES_STM32MP1X
#define SPI_STM32_ERR_MSK (LL_SPI_SR_UDR | LL_SPI_SR_CRCE | LL_SPI_SR_MODF | \
			   LL_SPI_SR_OVR | LL_SPI_SR_TIFRE)
#else
#if defined(LL_SPI_SR_UDR)
#define SPI_STM32_ERR_MSK (LL_SPI_SR_UDR | LL_SPI_SR_CRCERR | LL_SPI_SR_MODF | \
			   LL_SPI_SR_OVR | LL_SPI_SR_FRE)
#elif defined(SPI_SR_FRE)
#define SPI_STM32_ERR_MSK (LL_SPI_SR_CRCERR | LL_SPI_SR_MODF | \
			   LL_SPI_SR_OVR | LL_SPI_SR_FRE)
#else
#define SPI_STM32_ERR_MSK (LL_SPI_SR_CRCERR | LL_SPI_SR_MODF | LL_SPI_SR_OVR)
#endif
#endif /* CONFIG_SOC_SERIES_STM32MP1X */

/* Value to shift out when no application data needs transmitting. */
#define SPI_STM32_TX_NOP 0x00

static bool spi_stm32_transfer_ongoing(struct spi_stm32_data *data)
{
	return spi_context_tx_on(&data->ctx) || spi_context_rx_on(&data->ctx);
}

static int spi_stm32_get_err(SPI_TypeDef *spi)
{
	u32_t sr = LL_SPI_ReadReg(spi, SR);

	if (sr & SPI_STM32_ERR_MSK) {
		LOG_ERR("%s: err=%d", __func__,
			    sr & (u32_t)SPI_STM32_ERR_MSK);

		/* OVR error must be explicitly cleared */
		if (LL_SPI_IsActiveFlag_OVR(spi)) {
			LL_SPI_ClearFlag_OVR(spi);
		}

		return -EIO;
	}

	return 0;
}

/* Shift a SPI frame as master. */
static void spi_stm32_shift_m(SPI_TypeDef *spi, struct spi_stm32_data *data)
{
	u16_t tx_frame = SPI_STM32_TX_NOP;
	u16_t rx_frame;

	while (!ll_func_tx_is_empty(spi)) {
		/* NOP */
	}

#ifdef CONFIG_SOC_SERIES_STM32MP1X
	/* With the STM32MP1, if the device is the SPI master, we need to enable
	 * the start of the transfer with LL_SPI_StartMasterTransfer(spi)
	 */
	if (LL_SPI_GetMode(spi) == LL_SPI_MODE_MASTER) {
		LL_SPI_StartMasterTransfer(spi);
		while (!LL_SPI_IsActiveMasterTransfer(spi)) {
			/* NOP */
		}
	}
#endif

	if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
		if (spi_context_tx_buf_on(&data->ctx)) {
			tx_frame = UNALIGNED_GET((u8_t *)(data->ctx.tx_buf));
		}
		LL_SPI_TransmitData8(spi, tx_frame);
		/* The update is ignored if TX is off. */
		spi_context_update_tx(&data->ctx, 1, 1);
	} else {
		if (spi_context_tx_buf_on(&data->ctx)) {
			tx_frame = UNALIGNED_GET((u16_t *)(data->ctx.tx_buf));
		}
		LL_SPI_TransmitData16(spi, tx_frame);
		/* The update is ignored if TX is off. */
		spi_context_update_tx(&data->ctx, 2, 1);
	}

	while (!ll_func_rx_is_not_empty(spi)) {
		/* NOP */
	}

	if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
		rx_frame = LL_SPI_ReceiveData8(spi);
		if (spi_context_rx_buf_on(&data->ctx)) {
			UNALIGNED_PUT(rx_frame, (u8_t *)data->ctx.rx_buf);
		}
		spi_context_update_rx(&data->ctx, 1, 1);
	} else {
		rx_frame = LL_SPI_ReceiveData16(spi);
		if (spi_context_rx_buf_on(&data->ctx)) {
			UNALIGNED_PUT(rx_frame, (u16_t *)data->ctx.rx_buf);
		}
		spi_context_update_rx(&data->ctx, 2, 1);
	}
}

/* Shift a SPI frame as slave. */
static void spi_stm32_shift_s(SPI_TypeDef *spi, struct spi_stm32_data *data)
{
	if (ll_func_tx_is_empty(spi) && spi_context_tx_on(&data->ctx)) {
		u16_t tx_frame;

		if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
			tx_frame = UNALIGNED_GET((u8_t *)(data->ctx.tx_buf));
			LL_SPI_TransmitData8(spi, tx_frame);
			spi_context_update_tx(&data->ctx, 1, 1);
		} else {
			tx_frame = UNALIGNED_GET((u16_t *)(data->ctx.tx_buf));
			LL_SPI_TransmitData16(spi, tx_frame);
			spi_context_update_tx(&data->ctx, 2, 1);
		}
	} else {
		ll_func_disable_int_tx_empty(spi);
	}

	if (ll_func_rx_is_not_empty(spi) &&
	    spi_context_rx_buf_on(&data->ctx)) {
		u16_t rx_frame;

		if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
			rx_frame = LL_SPI_ReceiveData8(spi);
			UNALIGNED_PUT(rx_frame, (u8_t *)data->ctx.rx_buf);
			spi_context_update_rx(&data->ctx, 1, 1);
		} else {
			rx_frame = LL_SPI_ReceiveData16(spi);
			UNALIGNED_PUT(rx_frame, (u16_t *)data->ctx.rx_buf);
			spi_context_update_rx(&data->ctx, 2, 1);
		}
	}
}

/*
 * Without a FIFO, we can only shift out one frame's worth of SPI
 * data, and read the response back.
 *
 * TODO: support 16-bit data frames.
 */
static int spi_stm32_shift_frames(SPI_TypeDef *spi, struct spi_stm32_data *data)
{
	u16_t operation = data->ctx.config->operation;

	if (SPI_OP_MODE_GET(operation) == SPI_OP_MODE_MASTER) {
		spi_stm32_shift_m(spi, data);
	} else {
		spi_stm32_shift_s(spi, data);
	}

	return spi_stm32_get_err(spi);
}

static void spi_stm32_complete(struct spi_stm32_data *data, SPI_TypeDef *spi,
			       int status)
{
#ifdef CONFIG_SPI_STM32_INTERRUPT
	ll_func_disable_int_tx_empty(spi);
	ll_func_disable_int_rx_not_empty(spi);
	ll_func_disable_int_errors(spi);
#endif

	spi_context_cs_control(&data->ctx, false);

#if defined(CONFIG_SPI_STM32_HAS_FIFO)
	/* Flush RX buffer */
	while (ll_func_rx_is_not_empty(spi)) {
		(void) LL_SPI_ReceiveData8(spi);
	}
#endif

	if (LL_SPI_GetMode(spi) == LL_SPI_MODE_MASTER) {
		while (ll_func_spi_is_busy(spi)) {
			/* NOP */
		}
	}
	/* BSY flag is cleared when MODF flag is raised */
	if (LL_SPI_IsActiveFlag_MODF(spi)) {
		LL_SPI_ClearFlag_MODF(spi);
	}

	ll_func_disable_spi(spi);

#ifdef CONFIG_SPI_STM32_INTERRUPT
	spi_context_complete(&data->ctx, status);
#endif
}

#ifdef CONFIG_SPI_STM32_INTERRUPT
static void spi_stm32_isr(void *arg)
{
	struct device * const dev = (struct device *) arg;
	const struct spi_stm32_config *cfg = dev->config->config_info;
	struct spi_stm32_data *data = dev->driver_data;
	SPI_TypeDef *spi = cfg->spi;
	int err;

	err = spi_stm32_get_err(spi);
	if (err) {
		spi_stm32_complete(data, spi, err);
		return;
	}

	if (spi_stm32_transfer_ongoing(data)) {
		err = spi_stm32_shift_frames(spi, data);
	}

	if (err || !spi_stm32_transfer_ongoing(data)) {
		spi_stm32_complete(data, spi, err);
	}
}
#endif

static int spi_stm32_configure(struct device *dev,
			       const struct spi_config *config)
{
	const struct spi_stm32_config *cfg = DEV_CFG(dev);
	struct spi_stm32_data *data = DEV_DATA(dev);
	const u32_t scaler[] = {
		LL_SPI_BAUDRATEPRESCALER_DIV2,
		LL_SPI_BAUDRATEPRESCALER_DIV4,
		LL_SPI_BAUDRATEPRESCALER_DIV8,
		LL_SPI_BAUDRATEPRESCALER_DIV16,
		LL_SPI_BAUDRATEPRESCALER_DIV32,
		LL_SPI_BAUDRATEPRESCALER_DIV64,
		LL_SPI_BAUDRATEPRESCALER_DIV128,
		LL_SPI_BAUDRATEPRESCALER_DIV256
	};
	SPI_TypeDef *spi = cfg->spi;
	u32_t clock;
	int br;

	if (spi_context_configured(&data->ctx, config)) {
		/* Nothing to do */
		return 0;
	}

	if ((SPI_WORD_SIZE_GET(config->operation) != 8)
	    && (SPI_WORD_SIZE_GET(config->operation) != 16)) {
		return -ENOTSUP;
	}

	if (clock_control_get_rate(device_get_binding(STM32_CLOCK_CONTROL_NAME),
			(clock_control_subsys_t) &cfg->pclken, &clock) < 0) {
		LOG_ERR("Failed call clock_control_get_rate");
		return -EIO;
	}

	for (br = 1 ; br <= ARRAY_SIZE(scaler) ; ++br) {
		u32_t clk = clock >> br;

		if (clk <= config->frequency) {
			break;
		}
	}

	if (br > ARRAY_SIZE(scaler)) {
		LOG_ERR("Unsupported frequency %uHz, max %uHz, min %uHz",
			    config->frequency,
			    clock >> 1,
			    clock >> ARRAY_SIZE(scaler));
		return -EINVAL;
	}

	LL_SPI_Disable(spi);
	LL_SPI_SetBaudRatePrescaler(spi, scaler[br - 1]);

	if (SPI_MODE_GET(config->operation) & SPI_MODE_CPOL) {
		LL_SPI_SetClockPolarity(spi, LL_SPI_POLARITY_HIGH);
	} else {
		LL_SPI_SetClockPolarity(spi, LL_SPI_POLARITY_LOW);
	}

	if (SPI_MODE_GET(config->operation) & SPI_MODE_CPHA) {
		LL_SPI_SetClockPhase(spi, LL_SPI_PHASE_2EDGE);
	} else {
		LL_SPI_SetClockPhase(spi, LL_SPI_PHASE_1EDGE);
	}

	LL_SPI_SetTransferDirection(spi, LL_SPI_FULL_DUPLEX);

	if (config->operation & SPI_TRANSFER_LSB) {
		LL_SPI_SetTransferBitOrder(spi, LL_SPI_LSB_FIRST);
	} else {
		LL_SPI_SetTransferBitOrder(spi, LL_SPI_MSB_FIRST);
	}

	LL_SPI_DisableCRC(spi);

	if (config->cs || !IS_ENABLED(CONFIG_SPI_STM32_USE_HW_SS)) {
		LL_SPI_SetNSSMode(spi, LL_SPI_NSS_SOFT);
	} else {
		if (config->operation & SPI_OP_MODE_SLAVE) {
			LL_SPI_SetNSSMode(spi, LL_SPI_NSS_HARD_INPUT);
		} else {
			LL_SPI_SetNSSMode(spi, LL_SPI_NSS_HARD_OUTPUT);
		}
	}

	if (config->operation & SPI_OP_MODE_SLAVE) {
		LL_SPI_SetMode(spi, LL_SPI_MODE_SLAVE);
	} else {
		LL_SPI_SetMode(spi, LL_SPI_MODE_MASTER);
	}

	if (SPI_WORD_SIZE_GET(config->operation) ==  8) {
		LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_8BIT);
	} else {
		LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_16BIT);
	}

#if defined(CONFIG_SPI_STM32_HAS_FIFO)
	ll_func_set_fifo_threshold_8bit(spi);
#endif

#ifndef CONFIG_SOC_SERIES_STM32F1X
	LL_SPI_SetStandard(spi, LL_SPI_PROTOCOL_MOTOROLA);
#endif

	/* At this point, it's mandatory to set this on the context! */
	data->ctx.config = config;

	spi_context_cs_configure(&data->ctx);

	LOG_DBG("Installed config %p: freq %uHz (div = %u),"
		    " mode %u/%u/%u, slave %u",
		    config, clock >> br, 1 << br,
		    (SPI_MODE_GET(config->operation) & SPI_MODE_CPOL) ? 1 : 0,
		    (SPI_MODE_GET(config->operation) & SPI_MODE_CPHA) ? 1 : 0,
		    (SPI_MODE_GET(config->operation) & SPI_MODE_LOOP) ? 1 : 0,
		    config->slave);

	return 0;
}

static int spi_stm32_release(struct device *dev,
			     const struct spi_config *config)
{
	struct spi_stm32_data *data = DEV_DATA(dev);

	spi_context_unlock_unconditionally(&data->ctx);

	return 0;
}

static int transceive(struct device *dev,
		      const struct spi_config *config,
		      const struct spi_buf_set *tx_bufs,
		      const struct spi_buf_set *rx_bufs,
		      bool asynchronous, struct k_poll_signal *signal)
{
	const struct spi_stm32_config *cfg = DEV_CFG(dev);
	struct spi_stm32_data *data = DEV_DATA(dev);
	SPI_TypeDef *spi = cfg->spi;
	int ret;

	if (!tx_bufs && !rx_bufs) {
		return 0;
	}

#ifndef CONFIG_SPI_STM32_INTERRUPT
	if (asynchronous) {
		return -ENOTSUP;
	}
#endif

	spi_context_lock(&data->ctx, asynchronous, signal);

	ret = spi_stm32_configure(dev, config);
	if (ret) {
		return ret;
	}

	/* Set buffers info */
	spi_context_buffers_setup(&data->ctx, tx_bufs, rx_bufs, 1);

#if defined(CONFIG_SPI_STM32_HAS_FIFO)
	/* Flush RX buffer */
	while (ll_func_rx_is_not_empty(spi)) {
		(void) LL_SPI_ReceiveData8(spi);
	}
#endif

	LL_SPI_Enable(spi);

	/* This is turned off in spi_stm32_complete(). */
	spi_context_cs_control(&data->ctx, true);

#ifdef CONFIG_SPI_STM32_INTERRUPT
	ll_func_enable_int_errors(spi);

	if (rx_bufs) {
		ll_func_enable_int_rx_not_empty(spi);
	}

	ll_func_enable_int_tx_empty(spi);

	ret = spi_context_wait_for_completion(&data->ctx);
#else
	do {
		ret = spi_stm32_shift_frames(spi, data);
	} while (!ret && spi_stm32_transfer_ongoing(data));

	spi_stm32_complete(data, spi, ret);

#ifdef CONFIG_SPI_SLAVE
	if (spi_context_is_slave(&data->ctx) && !ret) {
		ret = data->ctx.recv_frames;
	}
#endif /* CONFIG_SPI_SLAVE */

#endif

	spi_context_release(&data->ctx, ret);

	return ret;
}

static int spi_stm32_transceive(struct device *dev,
				const struct spi_config *config,
				const struct spi_buf_set *tx_bufs,
				const struct spi_buf_set *rx_bufs)
{
	return transceive(dev, config, tx_bufs, rx_bufs, false, NULL);
}

#ifdef CONFIG_SPI_ASYNC
static int spi_stm32_transceive_async(struct device *dev,
				      const struct spi_config *config,
				      const struct spi_buf_set *tx_bufs,
				      const struct spi_buf_set *rx_bufs,
				      struct k_poll_signal *async)
{
	return transceive(dev, config, tx_bufs, rx_bufs, true, async);
}
#endif /* CONFIG_SPI_ASYNC */

static const struct spi_driver_api api_funcs = {
	.transceive = spi_stm32_transceive,
#ifdef CONFIG_SPI_ASYNC
	.transceive_async = spi_stm32_transceive_async,
#endif
	.release = spi_stm32_release,
};

static int spi_stm32_init(struct device *dev)
{
	struct spi_stm32_data *data __attribute__((unused)) = dev->driver_data;
	const struct spi_stm32_config *cfg = dev->config->config_info;

	__ASSERT_NO_MSG(device_get_binding(STM32_CLOCK_CONTROL_NAME));

	if (clock_control_on(device_get_binding(STM32_CLOCK_CONTROL_NAME),
			       (clock_control_subsys_t) &cfg->pclken) != 0) {
		LOG_ERR("Could not enable SPI clock");
		return -EIO;
	}

#ifdef CONFIG_SPI_STM32_INTERRUPT
	cfg->irq_config(dev);
#endif

	spi_context_unlock_unconditionally(&data->ctx);

	return 0;
}

#ifdef CONFIG_SPI_STM32_INTERRUPT
#define STM32_SPI_IRQ_HANDLER_DECL(id)					\
	static void spi_stm32_irq_config_func_##id(struct device *dev)
#define STM32_SPI_IRQ_HANDLER_FUNC(id)					\
	.irq_config = spi_stm32_irq_config_func_##id,
#define STM32_SPI_IRQ_HANDLER(id)					\
static void spi_stm32_irq_config_func_##id(struct device *dev)		\
{									\
	IRQ_CONNECT(DT_SPI_##id##_IRQ, DT_SPI_##id##_IRQ_PRI,		\
		    spi_stm32_isr, DEVICE_GET(spi_stm32_##id), 0);	\
	irq_enable(DT_SPI_##id##_IRQ);					\
}
#else
#define STM32_SPI_IRQ_HANDLER_DECL(id)
#define STM32_SPI_IRQ_HANDLER_FUNC(id)
#define STM32_SPI_IRQ_HANDLER(id)
#endif

#define STM32_SPI_INIT(id)						\
STM32_SPI_IRQ_HANDLER_DECL(id);						\
									\
static const struct spi_stm32_config spi_stm32_cfg_##id = {		\
	.spi = (SPI_TypeDef *) DT_SPI_##id##_BASE_ADDRESS,		\
	.pclken = {							\
		.enr = DT_SPI_##id##_CLOCK_BITS,			\
		.bus = DT_SPI_##id##_CLOCK_BUS				\
	},								\
	STM32_SPI_IRQ_HANDLER_FUNC(id)					\
};									\
									\
static struct spi_stm32_data spi_stm32_dev_data_##id = {		\
	SPI_CONTEXT_INIT_LOCK(spi_stm32_dev_data_##id, ctx),		\
	SPI_CONTEXT_INIT_SYNC(spi_stm32_dev_data_##id, ctx),		\
};									\
									\
DEVICE_AND_API_INIT(spi_stm32_##id, DT_SPI_##id##_NAME, &spi_stm32_init, \
		    &spi_stm32_dev_data_##id, &spi_stm32_cfg_##id,	\
		    POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,		\
		    &api_funcs);					\
									\
STM32_SPI_IRQ_HANDLER(id)

#ifdef CONFIG_SPI_1
STM32_SPI_INIT(1)
#endif

#ifdef CONFIG_SPI_2
STM32_SPI_INIT(2)
#endif

#ifdef CONFIG_SPI_3
STM32_SPI_INIT(3)
#endif

#ifdef CONFIG_SPI_4
STM32_SPI_INIT(4)
#endif

#ifdef CONFIG_SPI_5
STM32_SPI_INIT(5)
#endif

#ifdef CONFIG_SPI_6
STM32_SPI_INIT(6)
#endif
