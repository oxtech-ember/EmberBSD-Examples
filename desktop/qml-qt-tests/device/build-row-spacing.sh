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
input=$src/upstream/qtdeclarative-v6.12.0/horizontal-spacing.qml
mkdir -p "$OUT/obj" "$OUT/generated"

"$python" "$sdk/scripts/qml-compile.py" "$input" \
	--name horizontalspacing --output "$OUT/generated/horizontal-spacing.c"

cc()
{
	"$compiler" --target=riscv32--netbsd --sysroot="$SYSROOT" \
	    -march=rv32imac -mabi=ilp32 -Oz -ffunction-sections \
	    -fdata-sections "$@"
}

cc -std=c11 -Wall -Wextra -Werror -I"$runtime_qml" \
	-c "$runtime_qml/eqml.c" -o "$OUT/obj/eqml.o"
cc -std=c11 -Wall -Wextra -Werror -I"$runtime_qml" \
	-c "$OUT/generated/horizontal-spacing.c" -o "$OUT/obj/scene.o"
cc -std=c11 -Wall -Wextra -Werror -I"$runtime_qml" \
	-c "$src/device/row-spacing-smoke.c" -o "$OUT/obj/smoke.o"
cc -std=c11 -Wall -Wextra -Werror -I"$runtime_qml" \
	-c "$runtime_qml/eqml_wsdisplay.c" -o "$OUT/obj/eqml_wsdisplay.o"
cc -std=c11 -Wall -Wextra -Werror -I"$runtime_qml" \
	-c "$src/device/row-spacing-display.c" -o "$OUT/obj/display.o"

lib=$SYSROOT/usr/lib
for app in smoke display; do
	case "$app" in
	smoke) extra="$OUT/obj/smoke.o" ;;
	display) extra="$OUT/obj/display.o" ;;
	esac
	set -- "$OUT/obj/eqml.o" "$OUT/obj/scene.o"
	if [ "$app" = display ]; then
		set -- "$@" "$OUT/obj/eqml_wsdisplay.o"
	fi
	cc --ld-path="$HOST_PREFIX/bin/ld.lld" -static -nostdlib \
	    -Wl,--gc-sections,-s -o "$OUT/row-spacing-$app" \
	    "$lib/crt0.o" "$lib/crti.o" "$lib/crtbegin.o" "$extra" \
	    -Wl,--start-group "$@" "$lib/libc.a" "$BUILTINS" "$UNWIND" \
	    -Wl,--end-group "$lib/crtend.o" "$lib/crtn.o"
done
