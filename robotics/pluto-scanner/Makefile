# SPDX-License-Identifier: BSD-2-Clause
# Set CC and CPPFLAGS to the selected compiler and application sysroot.
CC ?= gcc
CFLAGS ?= -O2
WARN = -std=c11 -Wall -Wextra -Werror

all: pluto-scan

pluto-scan: src/pluto_scan.c src/scan.c src/scan.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) src/pluto_scan.c src/scan.c $(LDFLAGS) -lm -o $@

check:
	CC="$(CC)" CFLAGS="$(CPPFLAGS) $(CFLAGS)" sh tests/run.sh

clean:
	rm -f pluto-scan

.PHONY: all check clean
