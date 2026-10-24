# SPDX-License-Identifier: BSD-2-Clause
CC ?= gcc
CFLAGS ?= -O2
WARN = -std=c11 -Wall -Wextra -Werror
SOURCES = src/pluto_iiod.c src/iio_rx.c src/native_adc.c src/native_usb.c

all: pluto-iiod

pluto-iiod: $(SOURCES) src/iio_rx.h src/native_adc.h src/native_usb.h
	@case "$(CC)" in /*) ;; *) echo 'Set CC to the explicit GCC16 path' >&2; exit 1;; esac
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(SOURCES) $(LDFLAGS) -pthread -lm -o $@

check:
	CC="$(CC)" CFLAGS="$(CFLAGS)" sh tests/run.sh

clean:
	rm -f pluto-iiod

.PHONY: all check clean
