#ifndef NO_OS_DELAY_H
#define NO_OS_DELAY_H

#include <stdint.h>
#include "main.h"

static inline void no_os_mdelay(uint32_t ms)
{
	LL_mDelay(ms);
}

#endif
