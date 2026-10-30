#ifndef NO_OS_ALLOC_H
#define NO_OS_ALLOC_H

#include <stdlib.h>

static inline void *no_os_calloc(size_t nmemb, size_t size)
{
	return calloc(nmemb, size);
}

static inline void no_os_free(void *ptr)
{
	free(ptr);
}

#endif
