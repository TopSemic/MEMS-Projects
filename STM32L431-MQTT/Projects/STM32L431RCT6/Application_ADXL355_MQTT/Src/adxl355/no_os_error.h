#ifndef NO_OS_ERROR_H
#define NO_OS_ERROR_H

#include <errno.h>

/* Provide POSIX-like error codes when toolchain C library doesn't expose them. */
#ifndef EINVAL
#define EINVAL 22
#endif

#ifndef ENOMEM
#define ENOMEM 12
#endif

#ifndef EIO
#define EIO 5
#endif

#ifndef EAGAIN
#define EAGAIN 11
#endif

#ifndef ENOTSUP
#define ENOTSUP 95
#endif

#endif
