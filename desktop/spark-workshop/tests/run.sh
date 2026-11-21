#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
: "${OUT:=${TMPDIR:-/tmp}/spark-workshop-tests}"
mkdir -p "$OUT"
cc=${CC:-clang}
flags="-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer"

"$cc" $flags -I"$src" -o "$OUT/guide" \
	"$src/tests/test_guide.c" "$src/guide.c"
"$OUT/guide" "$src/notebook/connector.md"

"$cc" $flags -I"$src" -o "$OUT/camera-bmp" \
	"$src/tests/test_camera_bmp.c" "$src/camera-bmp.c"
"$OUT/camera-bmp"

if [ "$#" -gt 1 ]; then
	echo "usage: $0 [QML_GENERATED_DIR]" >&2
	exit 2
fi
if [ "$#" -eq 1 ]; then
	: "${EMBER_RUNTIME_SOURCE:?set the Runtime source for the QML host test}"
	generated=$1
	"$cc" $flags -I"$EMBER_RUNTIME_SOURCE/qml" -I"$generated" -I"$src" \
	    -o "$OUT/qml-view" "$src/tests/test_view.c" "$src/view.c" \
	    "$generated/spark-workshop.c" \
	    "$EMBER_RUNTIME_SOURCE/qml/eqml.c" \
	    "$EMBER_RUNTIME_SOURCE/qml/eqml_input.c"
	"$OUT/qml-view"
fi
