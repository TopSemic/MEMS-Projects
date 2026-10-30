#ifndef NO_OS_SPI_H
#define NO_OS_SPI_H

#include <stdint.h>
#include <stdlib.h>
#include "no_os_error.h"

struct no_os_spi_platform_ops {
	int (*transfer_byte)(uint8_t tx, uint8_t *rx);
	void (*cs_low)(void);
	void (*cs_high)(void);
};

struct no_os_spi_init_param {
	uint32_t max_speed_hz;
	uint8_t bit_order;
	uint8_t mode;
	void *extra;
};

struct no_os_spi_desc {
	void *extra;
};

static inline int no_os_spi_init(struct no_os_spi_desc **desc,
				 const struct no_os_spi_init_param *param)
{
	struct no_os_spi_desc *d;

	if (!desc || !param || !param->extra)
		return -EINVAL;

	d = (struct no_os_spi_desc *)calloc(1, sizeof(*d));
	if (!d)
		return -ENOMEM;

	d->extra = param->extra;
	*desc = d;
	return 0;
}

static inline int no_os_spi_remove(struct no_os_spi_desc *desc)
{
	if (!desc)
		return -EINVAL;
	free(desc);
	return 0;
}

static inline int no_os_spi_write_and_read(struct no_os_spi_desc *desc,
					    uint8_t *data,
					    uint16_t bytes_number)
{
	uint16_t i;
	uint8_t rx;
	const struct no_os_spi_platform_ops *ops;

	if (!desc || !data)
		return -EINVAL;

	ops = (const struct no_os_spi_platform_ops *)desc->extra;
	if (!ops || !ops->transfer_byte)
		return -EINVAL;

	if (ops->cs_low)
		ops->cs_low();

	for (i = 0; i < bytes_number; i++) {
		if (ops->transfer_byte(data[i], &rx)) {
			if (ops->cs_high)
				ops->cs_high();
			return -EIO;
		}
		data[i] = rx;
	}

	if (ops->cs_high)
		ops->cs_high();

	return 0;
}

#endif
