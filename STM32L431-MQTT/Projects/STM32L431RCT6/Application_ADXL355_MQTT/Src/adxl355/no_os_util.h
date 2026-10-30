#ifndef NO_OS_UTIL_H
#define NO_OS_UTIL_H

#include <stdint.h>

#define NO_OS_BIT(x) (1UL << (x))
#define NO_OS_GENMASK(h, l) (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (31 - (h))))
#define NO_OS_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static inline uint32_t no_os_get_unaligned_be24(const uint8_t *p)
{
	return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static inline int64_t no_os_div_s64_rem(int64_t dividend, int32_t divisor, int32_t *remainder)
{
	int64_t q = dividend / divisor;
	if (remainder)
		*remainder = (int32_t)(dividend - q * divisor);
	return q;
}

#endif
