// SPDX-License-Identifier: GPL-2.0
/* ADS1299: reference Zephyr configuration, Linux 5.10 IIO transport. */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/spi/spi.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <asm/unaligned.h>

#define ADS_RDATAC 0x10
#define ADS_SDATAC 0x11
#define ADS_STOP   0x0a
#define ADS_RREG   0x20
#define ADS_WREG   0x40
#define ADS_FRAME_BYTES 27

struct ads1299_state {
	struct spi_device *spi;
	struct gpio_desc *start, *pwdn, *reset, *drdy;
	struct iio_trigger *trigger;
	struct mutex lock;
	int irq;
	bool running;
	struct {
		s32 ch[2];
		s64 timestamp __aligned(8);
	} scan;
	/* SPI buffers must not share cache lines with mutable state. */
	u8 tx[32] ____cacheline_aligned;
	u8 rx[32] ____cacheline_aligned;
};

/* Register 0x01 through 0x17, matching ref/bsp_drivers/ads1299.c. */
static const u8 ads1299_regs[] = {
	0x95, 0xc0, 0xec, 0x00, /* CONFIG1..3, LOFF */
	0x60, 0x60, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
	0x03, 0x00, 0x03, 0x00, 0x00, /* BIAS, LOFF sense/flip */
	0x00, 0x00, /* Read-only LOFF status; not written */
	0x00, 0x20, 0x00, 0x02, /* GPIO, MISC1/2, CONFIG4 */
};

/* Called under lock (or exclusively during probe). 1 MHz gives 8 us/byte,
 * exceeding the 4*tCLK command decode interval for a 2.048 MHz ADC clock.
 */
static int ads1299_command(struct ads1299_state *st, u8 command)
{
	struct spi_transfer xfer = {
		.tx_buf = st->tx, .len = 1, .delay_usecs = 3,
	};
	int ret;

	st->tx[0] = command;
	ret = spi_sync_transfer(st->spi, &xfer, 1);
	usleep_range(3, 10);
	return ret;
}

static int ads1299_read_reg(struct ads1299_state *st, u8 reg, u8 *value)
{
	struct spi_transfer xfer = {
		.tx_buf = st->tx, .rx_buf = st->rx, .len = 3, .delay_usecs = 3,
	};
	int ret;

	st->tx[0] = ADS_RREG | reg;
	st->tx[1] = 0;
	st->tx[2] = 0;
	ret = spi_sync_transfer(st->spi, &xfer, 1);
	if (!ret)
		*value = st->rx[2];
	return ret;
}

static int ads1299_write_reg(struct ads1299_state *st, u8 reg, u8 value)
{
	struct spi_transfer xfer = {
		.tx_buf = st->tx, .len = 3, .delay_usecs = 3,
	};
	int ret;

	st->tx[0] = ADS_WREG | reg;
	st->tx[1] = 0;
	st->tx[2] = value;
	ret = spi_sync_transfer(st->spi, &xfer, 1);
	usleep_range(3, 10);
	return ret;
}

static int ads1299_init_hw(struct ads1299_state *st)
{
	u8 value, mask;
	int reg, ret;

	gpiod_set_value_cansleep(st->pwdn, 0);
	msleep(200);
	gpiod_set_value_cansleep(st->reset, 1);
	msleep(10);
	gpiod_set_value_cansleep(st->reset, 0);
	msleep(50);
	ret = ads1299_command(st, ADS_SDATAC);
	if (ret)
		return ret;
	ret = ads1299_command(st, ADS_STOP);
	if (ret)
		return ret;
	ret = ads1299_read_reg(st, 0, &value);
	if (ret)
		return ret;
	if ((value & 0x1f) != 0x1e) {
		dev_err(&st->spi->dev, "unexpected ADS1299 ID: 0x%02x\n", value);
		return -ENODEV;
	}
	for (reg = 1; reg <= ARRAY_SIZE(ads1299_regs); reg++) {
		if (reg == 0x12 || reg == 0x13)
			continue;
		ret = ads1299_write_reg(st, reg, ads1299_regs[reg - 1]);
		if (ret)
			return ret;
		ret = ads1299_read_reg(st, reg, &value);
		if (ret)
			return ret;
		/* CONFIG3 bit 0 is live BIAS status, not configuration. */
		mask = reg == 0x03 ? 0xfe : 0xff;
		if ((value & mask) != (ads1299_regs[reg - 1] & mask)) {
			dev_err(&st->spi->dev, "register 0x%02x: got %02x expected %02x\n",
				reg, value, ads1299_regs[reg - 1]);
			return -EIO;
		}
	}
	/* Allow the internal reference to settle before the first acquisition. */
	msleep(150);
	return 0;
}

static irqreturn_t ads1299_drdy_irq(int irq, void *data)
{
	struct ads1299_state *st = data;

	iio_trigger_poll(st->trigger);
	return IRQ_HANDLED;
}

static irqreturn_t ads1299_trigger_handler(int irq, void *data)
{
	struct iio_poll_func *pf = data;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ads1299_state *st = iio_priv(indio_dev);
	struct spi_transfer xfer = {
		.tx_buf = st->tx, .rx_buf = st->rx, .len = ADS_FRAME_BYTES,
		.delay_usecs = 3,
	};
	int ret;

	mutex_lock(&st->lock);
	if (!st->running)
		goto done;
	memset(st->tx, 0, ADS_FRAME_BYTES);
	ret = spi_sync_transfer(st->spi, &xfer, 1);
	if (ret) {
		dev_err_ratelimited(&st->spi->dev, "frame SPI error: %d\n", ret);
		goto done;
	}
	if ((st->rx[0] & 0xf0) != 0xc0) {
		dev_err_ratelimited(&st->spi->dev, "invalid frame status: %02x %02x %02x\n",
			st->rx[0], st->rx[1], st->rx[2]);
		goto done;
	}
	st->scan.ch[0] = sign_extend32(get_unaligned_be24(st->rx + 3), 23);
	st->scan.ch[1] = sign_extend32(get_unaligned_be24(st->rx + 6), 23);
	iio_push_to_buffers_with_timestamp(indio_dev, &st->scan, pf->timestamp);
done:
	mutex_unlock(&st->lock);
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int ads1299_set_trigger_state(struct iio_trigger *trigger, bool enable)
{
	struct ads1299_state *st = iio_trigger_get_drvdata(trigger);
	int ret = 0;

	if (!enable)
		disable_irq(st->irq);
	mutex_lock(&st->lock);
	if (enable) {
		ret = ads1299_command(st, ADS_RDATAC);
		if (!ret) {
			st->running = true;
			enable_irq(st->irq);
			/* Hardware START alone controls conversions. */
			gpiod_set_value_cansleep(st->start, 1);
		}
	} else {
		st->running = false;
		gpiod_set_value_cansleep(st->start, 0);
		ret = ads1299_command(st, ADS_SDATAC);
		if (ret)
			dev_err(&st->spi->dev, "stop: SDATAC failed: %d\n", ret);
		/* Always allow IIO to detach its IRQ consumer on shutdown. */
		ret = 0;
	}
	mutex_unlock(&st->lock);
	return ret;
}

static const struct iio_trigger_ops ads1299_trigger_ops = {
	.set_trigger_state = ads1299_set_trigger_state,
	.validate_device = iio_trigger_validate_own_device,
};

static int ads1299_read_raw(struct iio_dev *indio_dev,
			  const struct iio_chan_spec *chan,
			  int *val, int *val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		/* mV/code, matching the reference application's 2^23-1 divisor. */
		*val = 4500;
		*val2 = 24 * 8388607;
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = 500;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

#define ADS_CHANNEL(n) { \
	.type = IIO_VOLTAGE, .indexed = 1, .channel = (n) + 1, \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_index = (n), \
	.scan_type = { .sign = 's', .realbits = 24, .storagebits = 32, \
		.endianness = IIO_CPU }, \
}

static const struct iio_chan_spec ads1299_channels[] = {
	ADS_CHANNEL(0), ADS_CHANNEL(1), IIO_CHAN_SOFT_TIMESTAMP(2),
};
static const unsigned long ads1299_scan_masks[] = { BIT(0) | BIT(1), 0 };
static const struct iio_info ads1299_info = { .read_raw = ads1299_read_raw };
static const struct iio_buffer_setup_ops ads1299_buffer_ops = { };

static void ads1299_power_off(void *data)
{
	struct ads1299_state *st = data;

	gpiod_set_value_cansleep(st->start, 0);
	gpiod_set_value_cansleep(st->pwdn, 1);
}

static int ads1299_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ads1299_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	st = iio_priv(indio_dev);
	st->spi = spi;
	mutex_init(&st->lock);
	spi->mode = SPI_MODE_1;
	spi->bits_per_word = 8;
	spi->max_speed_hz = min(spi->max_speed_hz, 1000000U);
	ret = spi_setup(spi);
	if (ret)
		return ret;
	st->start = devm_gpiod_get(&spi->dev, "start", GPIOD_OUT_LOW);
	if (IS_ERR(st->start))
		return PTR_ERR(st->start);
	st->pwdn = devm_gpiod_get(&spi->dev, "pwdn", GPIOD_OUT_HIGH);
	if (IS_ERR(st->pwdn))
		return PTR_ERR(st->pwdn);
	st->reset = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(st->reset))
		return PTR_ERR(st->reset);
	ret = devm_add_action_or_reset(&spi->dev, ads1299_power_off, st);
	if (ret)
		return ret;
	st->drdy = devm_gpiod_get(&spi->dev, "drdy", GPIOD_IN);
	if (IS_ERR(st->drdy))
		return PTR_ERR(st->drdy);
	st->irq = gpiod_to_irq(st->drdy);
	if (st->irq < 0)
		return st->irq;
	ret = ads1299_init_hw(st);
	if (ret)
		return ret;
	indio_dev->name = "ads1299";
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &ads1299_info;
	indio_dev->channels = ads1299_channels;
	indio_dev->num_channels = ARRAY_SIZE(ads1299_channels);
	indio_dev->available_scan_masks = ads1299_scan_masks;
	st->trigger = devm_iio_trigger_alloc(&spi->dev, "ads1299-%s", dev_name(&spi->dev));
	if (!st->trigger)
		return -ENOMEM;
	st->trigger->dev.parent = &spi->dev;
	st->trigger->ops = &ads1299_trigger_ops;
	iio_trigger_set_drvdata(st->trigger, st);
	ret = devm_iio_trigger_register(&spi->dev, st->trigger);
	if (ret)
		return ret;
	/* START is low; no conversions can race the initial disable_irq. */
	ret = devm_request_irq(&spi->dev, st->irq, ads1299_drdy_irq,
			       IRQF_TRIGGER_FALLING, "ads1299-drdy", st);
	if (ret)
		return ret;
	disable_irq(st->irq);
	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev,
			iio_pollfunc_store_time, ads1299_trigger_handler, &ads1299_buffer_ops);
	if (ret)
		return ret;
	/* Buffer setup establishes the mode used to release this trigger ref. */
	ret = iio_trigger_set_immutable(indio_dev, st->trigger);
	if (ret)
		return ret;
	ret = devm_iio_device_register(&spi->dev, indio_dev);
	if (!ret)
		dev_info(&spi->dev, "ADS1299 ready: CH1/CH2, 500 SPS, gain 24\n");
	return ret;
}

static const struct of_device_id ads1299_of_match[] = {
	{ .compatible = "ti,ads1299" }, { }
};
MODULE_DEVICE_TABLE(of, ads1299_of_match);
static const struct spi_device_id ads1299_ids[] = { { "ads1299", 0 }, { } };
MODULE_DEVICE_TABLE(spi, ads1299_ids);
static struct spi_driver ads1299_driver = {
	.driver = { .name = "ads1299-myboard", .of_match_table = ads1299_of_match },
	.probe = ads1299_probe,
	.id_table = ads1299_ids,
};
module_spi_driver(ads1299_driver);
MODULE_DESCRIPTION("ADS1299 MyBoard two-channel IIO driver");
MODULE_LICENSE("GPL");
