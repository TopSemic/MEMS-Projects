#ifndef NO_OS_I2C_H
#define NO_OS_I2C_H

#include <stdint.h>
#include "no_os_error.h"

struct no_os_i2c_init_param {
	uint8_t dummy;
};

struct no_os_i2c_desc {
	uint8_t dummy;
};

static inline int no_os_i2c_init(struct no_os_i2c_desc **desc,
				 const struct no_os_i2c_init_param *param)
{
	(void)desc;
	(void)param;
	return -ENOTSUP;
}

static inline int no_os_i2c_remove(struct no_os_i2c_desc *desc)
{
	(void)desc;
	return 0;
}

static inline int no_os_i2c_write(struct no_os_i2c_desc *desc,
				  uint8_t *data,
				  uint16_t bytes_number,
				  uint8_t stop_bit)
{
	(void)desc;
	(void)data;
	(void)bytes_number;
	(void)stop_bit;
	return -ENOTSUP;
}

static inline int no_os_i2c_read(struct no_os_i2c_desc *desc,
				 uint8_t *data,
				 uint16_t bytes_number,
				 uint8_t stop_bit)
{
	(void)desc;
	(void)data;
	(void)bytes_number;
	(void)stop_bit;
	return -ENOTSUP;
}

#endif
