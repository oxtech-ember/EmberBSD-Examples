#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu

src=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
runtime=${EMBER_RUNTIME_SOURCE:?set EmberBSD-Runtime source}
sdk=${EMBER_SDK_SOURCE:?set EmberBSD-SDK source}
: "${HOST_PREFIX:?set the native Ports prefix}" "${SYSROOT:?set the RV32 sysroot}"
: "${BUILTINS:?set the RV32 compiler builtins archive}"
: "${UNWIND:?set the RV32 unwind archive}" "${OUT:?set output directory}"

compiler=${TARGET_CLANG:-$HOST_PREFIX/bin/clang}
python=${PYTHON:-$HOST_PREFIX/bin/python3.15}
runtime_qml=$runtime/qml
scene=${1:-mousearea}
case "$scene" in
mousearea|behavior) symbol=$scene ;;
visual-opacity) symbol=visualopacity ;;
*) echo "usage: $0 [mousearea|behavior|visual-opacity]" >&2; exit 2 ;;
esac
input=$src/upstream/qtdeclarative-v6.12.0/$scene.qml
mkdir -p "$OUT/obj" "$OUT/generated"

"$python" "$sdk/scripts/qml-compile.py" "$input" \
	--name "$symbol" --output "$OUT/generated/$scene.c"

cc()
{
	"$compiler" --target=riscv32--netbsd --sysroot="$SYSROOT" \
	    -march=rv32imac -mabi=ilp32 -Oz -ffunction-sections \
	    -fdata-sections "$@"
}

for unit in eqml eqml_input eqml_wsdisplay eqml_wsmouse; do
	cc -std=c11 -Wall -Wextra -Werror -I"$runtime_qml" \
	    -c "$runtime_qml/$unit.c" -o "$OUT/obj/$unit.o"
done
cc -std=c11 -Wall -Wextra -Werror -I"$runtime_qml" \
	-c "$OUT/generated/$scene.c" -o "$OUT/obj/$scene-scene.o"
cc -std=c11 -Wall -Wextra -Werror -I"$runtime_qml" \
	-DSCENE_SYMBOL=eqml_"$symbol" -DSCENE_NAME="\"$scene\"" \
	-c "$src/device/mousearea.c" -o "$OUT/obj/$scene-app.o"

lib=$SYSROOT/usr/lib
cc --ld-path="$HOST_PREFIX/bin/ld.lld" -static -nostdlib \
	-Wl,--gc-sections,-s -o "$OUT/$scene" \
	"$lib/crt0.o" "$lib/crti.o" "$lib/crtbegin.o" \
	"$OUT/obj/$scene-app.o" -Wl,--start-group \
	"$OUT/obj/eqml.o" "$OUT/obj/eqml_input.o" \
	"$OUT/obj/eqml_wsdisplay.o" "$OUT/obj/eqml_wsmouse.o" \
	"$OUT/obj/$scene-scene.o" "$lib/libc.a" "$BUILTINS" "$UNWIND" \
	-Wl,--end-group "$lib/crtend.o" "$lib/crtn.o"
