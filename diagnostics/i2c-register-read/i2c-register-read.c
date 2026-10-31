/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026, EmberBSD contributors. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <dev/i2c/i2c_io.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_READ 256

static int
number(const char *s, unsigned long max, unsigned long *result)
{
	char *end;
	unsigned long n;

	if (*s == '\0' || *s == '-' || *s == '+' || *s == ' ' ||
	    *s == '\t' || *s == '\n')
		return -1;
	errno = 0;
	n = strtoul(s, &end, 0);
	if (errno != 0 || *end != '\0' || n > max)
		return -1;
	*result = n;
	return 0;
}

int
main(int argc, char **argv)
{
	i2c_ioctl_exec_t req = {0};
	unsigned char cmd[2], data[MAX_READ];
	unsigned long addr, width, reg, count;
	int fd;
	size_t i;

	if (argc != 6 || number(argv[2], 0x7f, &addr) ||
	    number(argv[3], 2, &width) || width == 0 ||
	    number(argv[4], 0xffff, &reg) ||
	    (width == 1 && reg > 0xff) ||
	    number(argv[5], MAX_READ, &count) || count == 0) {
		fprintf(stderr, "usage: %s device address register-width register count\n", argv[0]);
		return 2;
	}
	cmd[0] = (unsigned char)(width == 2 ? reg >> 8 : reg);
	cmd[1] = (unsigned char)reg;
	fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		perror(argv[1]);
		return 1;
	}
	req.iie_op = I2C_OP_READ_WITH_STOP;
	req.iie_addr = (i2c_addr_t)addr;
	req.iie_cmd = cmd;
	req.iie_cmdlen = width;
	req.iie_buf = data;
	req.iie_buflen = count;
	if (ioctl(fd, I2C_IOCTL_EXEC, &req) == -1) {
		perror("I2C_IOCTL_EXEC");
		(void)close(fd);
		return 1;
	}
	if (close(fd) == -1) {
		perror("close");
		return 1;
	}
	for (i = 0; i < count; i++)
		printf("%s%02x", i ? " " : "", data[i]);
	putchar('\n');
	return 0;
}
