#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu

src=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
out=${OUT:-/tmp/qml-network-lab-tests}
cc=${CC:-cc}
mkdir -p "$out"
"$cc" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -pthread \
	-I"$src" "$src/sample.c" "$src/tests/test_sample.c" \
	-o "$out/test_sample"
"$out/test_sample"
