#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
: "${CC:?Set the explicit GCC16 compiler}"
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/pluto-scan.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$CC" -std=c11 -O2 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$dir/../src" "$dir/scan_test.c" "$dir/../src/scan.c" -lm -o "$work/test"
"$work/test"
# The adapter uses the installed public ABI header from the selected sysroot.
# Pass its include directory through CFLAGS, as for the application build.
"$CC" -std=c11 -O2 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$dir/../src" "$dir/adapter_test.c" "$dir/../src/scan.c" -lm -o "$work/adapter-test"
"$work/adapter-test"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror ${CFLAGS:-} \
    "$dir/iq_test.c" -o "$work/iq-test"
"$work/iq-test"
