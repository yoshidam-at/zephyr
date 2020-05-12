/*
 * Copyright (c) 2017 Linaro Limited
 * Copyright (c) 2019, Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/led_strip.h>

#include <string.h>

#define LOG_LEVEL CONFIG_LED_STRIP_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(ws2812_spi);

#include <zephyr.h>
#include <device.h>
#include <drivers/spi.h>
#include <sys/math_extras.h>
#include <sys/util.h>

/* spi-one-frame and spi-zero-frame in DT are for 8-bit frames. */
#define SPI_FRAME_BITS 8

/*
 * Delay time to make sure the strip has latched a signal.
 *
 * Despite datasheet claims, a 6 microsecond delay is enough to reset
 * the strip. Delay for 8 usec just to be safe.
 */
#define RESET_DELAY_USEC 8

/*
 * SPI master configuration:
 *
 * - mode 0 (the default), 8 bit, MSB first (arbitrary), one-line SPI
 * - no shenanigans (don't hold CS, don't hold the device lock, this
 *   isn't an EEPROM)
 */
#define SPI_OPER (SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | \
		  SPI_WORD_SET(SPI_FRAME_BITS) | SPI_LINES_SINGLE)

#define BYTES_PER_PX(has_white) ((has_white) ? 32 : 24)

struct ws2812_spi_data {
	struct device *spi;
};

struct ws2812_spi_cfg {
	struct spi_config spi_cfg;
	u8_t *px_buf;
	size_t px_buf_size;
	u8_t one_frame;
	u8_t zero_frame;
	bool has_white;
};

static struct ws2812_spi_data *dev_data(struct device *dev)
{
	return dev->driver_data;
}

static const struct ws2812_spi_cfg *dev_cfg(struct device *dev)
{
	return dev->config->config_info;
}

/*
 * Serialize an 8-bit color channel value into an equivalent sequence
 * of SPI frames, MSbit first, where a one bit becomes SPI frame
 * one_frame, and zero bit becomes zero_frame.
 */
static inline void ws2812_spi_ser(u8_t buf[8], u8_t color,
				  const u8_t one_frame, const u8_t zero_frame)
{
	int i;

	for (i = 0; i < 8; i++) {
		buf[i] = color & BIT(7 - i) ? one_frame : zero_frame;
	}
}

/*
 * Returns true if and only if cfg->px_buf is big enough to convert
 * num_pixels RGB color values into SPI frames.
 */
static inline bool num_pixels_ok(const struct ws2812_spi_cfg *cfg,
				 size_t num_pixels)
{
	size_t nbytes;
	bool overflow;

	overflow = size_mul_overflow(num_pixels, BYTES_PER_PX(cfg->has_white),
				     &nbytes);
	return !overflow && (nbytes <= cfg->px_buf_size);
}

/*
 * Latch current color values on strip and reset its state machines.
 */
static inline void ws2812_reset_delay(void)
{
	/*
	 * TODO: swap out with k_usleep() once that can be trusted to
	 * work reliably.
	 */
	k_busy_wait(RESET_DELAY_USEC);
}

static int ws2812_strip_update_rgb(struct device *dev, struct led_rgb *pixels,
				   size_t num_pixels)
{
	const struct ws2812_spi_cfg *cfg = dev_cfg(dev);
	const u8_t one = cfg->one_frame, zero = cfg->zero_frame;
	struct spi_buf buf = {
		.buf = cfg->px_buf,
		.len = cfg->px_buf_size,
	};
	const struct spi_buf_set tx = {
		.buffers = &buf,
		.count = 1
	};
	u8_t *px_buf = cfg->px_buf;
	size_t i;
	int rc;

	if (!num_pixels_ok(cfg, num_pixels)) {
		return -ENOMEM;
	}

	/*
	 * Convert pixel data into SPI frames. Each frame has pixel
	 * data in GRB on-wire format, with zeroed out white channel data
	 * if applicable.
	 */
	for (i = 0; i < num_pixels; i++) {
		ws2812_spi_ser(px_buf, pixels[i].g, one, zero);
		ws2812_spi_ser(px_buf + 8, pixels[i].r, one, zero);
		ws2812_spi_ser(px_buf + 16, pixels[i].b, one, zero);
		if (cfg->has_white) {
			ws2812_spi_ser(px_buf + 24, 0, one, zero);
			px_buf += 32;
		} else {
			px_buf += 24;
		}
	}

	/*
	 * Display the pixel data.
	 */
	rc = spi_write(dev_data(dev)->spi, &cfg->spi_cfg, &tx);
	ws2812_reset_delay();

	return rc;
}

static int ws2812_strip_update_channels(struct device *dev, u8_t *channels,
					size_t num_channels)
{
	LOG_ERR("update_channels not implemented");
	return -ENOTSUP;
}

static const struct led_strip_driver_api ws2812_spi_api = {
	.update_rgb = ws2812_strip_update_rgb,
	.update_channels = ws2812_strip_update_channels,
};

#define WS2812_SPI_LABEL(idx) \
	(DT_INST_##idx##_WORLDSEMI_WS2812_SPI_LABEL)
#define WS2812_SPI_NUM_PIXELS(idx) \
	(DT_INST_##idx##_WORLDSEMI_WS2812_SPI_CHAIN_LENGTH)
#define WS2812_SPI_HAS_WHITE(idx) \
	(DT_INST_##idx##_WORLDSEMI_WS2812_SPI_HAS_WHITE_CHANNEL == 1)
#define WS2812_SPI_BUS(idx) \
	(DT_INST_##idx##_WORLDSEMI_WS2812_SPI_BUS_NAME)
#define WS2812_SPI_SLAVE(idx) \
	(DT_INST_##idx##_WORLDSEMI_WS2812_SPI_BASE_ADDRESS)
#define WS2812_SPI_FREQ(idx) \
	(DT_INST_##idx##_WORLDSEMI_WS2812_SPI_SPI_MAX_FREQUENCY)
#define WS2812_SPI_ONE_FRAME(idx) \
	(DT_INST_##idx##_WORLDSEMI_WS2812_SPI_SPI_ONE_FRAME)
#define WS2812_SPI_ZERO_FRAME(idx)\
	(DT_INST_##idx##_WORLDSEMI_WS2812_SPI_SPI_ZERO_FRAME)
#define WS2812_SPI_BUFSZ(idx) \
	(BYTES_PER_PX(WS2812_SPI_HAS_WHITE(idx)) * WS2812_SPI_NUM_PIXELS(idx))

#define WS2812_SPI_DEVICE(idx)						\
									\
	static struct ws2812_spi_data ws2812_spi_##idx##_data;		\
									\
	static u8_t ws2812_spi_##idx##_px_buf[WS2812_SPI_BUFSZ(idx)];	\
									\
	static const struct ws2812_spi_cfg ws2812_spi_##idx##_cfg = {	\
		.spi_cfg = {						\
			.frequency = WS2812_SPI_FREQ(idx),		\
			.operation = SPI_OPER,				\
			.slave = WS2812_SPI_SLAVE(idx),		\
			.cs = NULL,					\
		},							\
		.px_buf = ws2812_spi_##idx##_px_buf,			\
		.px_buf_size = WS2812_SPI_BUFSZ(idx),			\
		.one_frame = WS2812_SPI_ONE_FRAME(idx),		\
		.zero_frame = WS2812_SPI_ZERO_FRAME(idx),		\
		.has_white = WS2812_SPI_HAS_WHITE(idx),		\
	};								\
									\
	static int ws2812_spi_##idx##_init(struct device *dev)		\
	{								\
		struct ws2812_spi_data *data = dev_data(dev);		\
									\
		data->spi = device_get_binding(WS2812_SPI_BUS(idx));	\
		if (!data->spi) {					\
			LOG_ERR("SPI device %s not found",		\
				WS2812_SPI_BUS(idx));			\
			return -ENODEV;				\
		}							\
									\
		return 0;						\
	}								\
									\
	DEVICE_AND_API_INIT(ws2812_spi_##idx,				\
			    WS2812_SPI_LABEL(idx),			\
			    ws2812_spi_##idx##_init,			\
			    &ws2812_spi_##idx##_data,			\
			    &ws2812_spi_##idx##_cfg,			\
			    POST_KERNEL,				\
			    CONFIG_LED_STRIP_INIT_PRIORITY,		\
			    &ws2812_spi_api);

#ifdef DT_INST_0_WORLDSEMI_WS2812_SPI_LABEL
WS2812_SPI_DEVICE(0);
#endif

#ifdef DT_INST_1_WORLDSEMI_WS2812_SPI_LABEL
WS2812_SPI_DEVICE(1);
#endif
