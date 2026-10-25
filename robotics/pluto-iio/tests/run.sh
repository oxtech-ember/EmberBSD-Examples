#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
: "${CC:?Select the absolute host GCC16 path}"
: "${ABI_INCLUDE:?Select the directory containing public dev/fdt and dev/usb headers}"
case "$CC" in /*) ;; *) echo 'CC must be absolute' >&2; exit 2;; esac
dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/pluto-iio.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for t in protocol adc usb; do
    case "$t" in
        protocol) sources="$dir/src/iio_rx.c";;
        adc) sources="$dir/src/native_adc.c";;
        usb) sources="$dir/src/native_usb.c";;
    esac
    "$CC" -D_DARWIN_C_SOURCE -D_POSIX_C_SOURCE=200809L -std=c11 \
        -Wall -Wextra -Werror ${CFLAGS:-} -I"$dir/src" -I"$dir/tests/include" \
        -I"$ABI_INCLUDE" "$dir/tests/$t.c" "$sources" -lm -o "$work/$t"
    "$work/$t"
done
"$CC" -D_DARWIN_C_SOURCE -D_POSIX_C_SOURCE=200809L -std=c11 \
    -Wall -Wextra -Werror ${CFLAGS:-} -I"$dir/src" -I"$dir/tests/include" \
    -I"$ABI_INCLUDE" "$dir/tests/worker.c" "$dir/src/iio_rx.c" \
    "$dir/src/native_adc.c" "$dir/src/native_usb.c" -pthread -lm -o "$work/worker"
"$work/worker"
"$CC" -D_DARWIN_C_SOURCE -D_POSIX_C_SOURCE=200809L -std=c11 \
    -Wall -Wextra -Werror ${CFLAGS:-} -I"$dir/src" -I"$dir/tests/include" \
    -I"$ABI_INCLUDE" "$dir/tests/cancel.c" "$dir/src/iio_rx.c" \
    "$dir/src/native_adc.c" "$dir/src/native_usb.c" -pthread -lm -o "$work/cancel"
for scenario in 1 2 3 4 5 6; do "$work/cancel" "$scenario"; done
"$CC" -D_DARWIN_C_SOURCE -D_POSIX_C_SOURCE=200809L -std=c11 \
    -Wall -Wextra -Werror ${CFLAGS:-} -I"$dir/src" -I"$dir/tests/include" \
    -I"$ABI_INCLUDE" "$dir/tests/startup.c" "$dir/src/iio_rx.c" \
    "$dir/src/native_adc.c" "$dir/src/native_usb.c" -pthread -lm -o "$work/startup"
for scenario in 1 2 3 4; do "$work/startup" "$scenario"; done
"$CC" -D_DARWIN_C_SOURCE -D_POSIX_C_SOURCE=200809L -std=c11 \
    -Wall -Wextra -Werror ${CFLAGS:-} -I"$dir/src" -I"$dir/tests/include" \
    -I"$ABI_INCLUDE" "$dir"/src/*.c -pthread -lm -o "$work/pluto-iiod"
"$work/pluto-iiod" --self-test
if [ -n "${LIBIIO:-}" ]; then
    : "${PYTHON:?Select the explicit Python interpreter for libiio checks}"
    "$CC" -D_DARWIN_C_SOURCE -D_POSIX_C_SOURCE=200809L -std=c11 \
        -Wall -Wextra -Werror ${CFLAGS:-} -I"$dir/src" -I"$dir/tests/include" \
        -I"$ABI_INCLUDE" "$dir/tests/host.c" "$dir/src/iio_rx.c" \
        "$dir/src/native_adc.c" -lm -o "$work/host"
    "$PYTHON" "$dir/tests/libiio.py" "$work/host" "$LIBIIO"
fi
