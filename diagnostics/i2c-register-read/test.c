/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026, EmberBSD contributors. */
#include <stdarg.h>
#include <string.h>

#define open fake_open
#define ioctl fake_ioctl
#define close fake_close
#define main example_main
#include "i2c-register-read.c"
#undef main
#undef open
#undef ioctl
#undef close

static int calls;
static int fail_ioctl;
static unsigned char expected[2];
static size_t expected_width;

int
fake_open(const char *path, int flags, ...)
{
	if (strcmp(path, "/test/iic") != 0 || flags != O_RDONLY)
		abort();
	return 9;
}

int
fake_ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	i2c_ioctl_exec_t *req;

	va_start(ap, request);
	req = va_arg(ap, i2c_ioctl_exec_t *);
	va_end(ap);
	if (fd != 9 || request != I2C_IOCTL_EXEC ||
	    req->iie_op != I2C_OP_READ_WITH_STOP || req->iie_addr != 0x20 ||
	    req->iie_cmdlen != expected_width || req->iie_buflen != 2 ||
	    memcmp(req->iie_cmd, expected, expected_width) != 0)
		abort();
	calls++;
	if (fail_ioctl) {
		errno = EIO;
		return -1;
	}
	((unsigned char *)req->iie_buf)[0] = 0xaa;
	((unsigned char *)req->iie_buf)[1] = 0xbb;
	return 0;
}

int
fake_close(int fd)
{
	if (fd != 9)
		abort();
	return 0;
}

int
main(void)
{
	char *args[] = {"i2c-register-read", "/test/iic", "0x20", "2",
	    "0x1234", "2", NULL};

	expected[0] = 0x12;
	expected[1] = 0x34;
	expected_width = 2;
	if (example_main(6, args) != 0 || calls != 1)
		return 1;
	args[3] = "1";
	args[4] = "0x34";
	expected[0] = 0x34;
	expected_width = 1;
	if (example_main(6, args) != 0 || calls != 2)
		return 1;
	fail_ioctl = 1;
	if (example_main(6, args) != 1 || calls != 3)
		return 1;
	args[5] = "257";
	if (example_main(6, args) != 2 || calls != 3)
		return 1;
	return 0;
}
