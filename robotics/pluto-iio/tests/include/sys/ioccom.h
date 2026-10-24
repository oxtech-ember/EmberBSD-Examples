/* SPDX-License-Identifier: BSD-2-Clause */
/* Host test ABI only. Native builds use the EmberBSD sysroot header. */
#ifdef __APPLE__
#include_next <sys/ioccom.h>
#else
#include <sys/ioctl.h>
#endif
