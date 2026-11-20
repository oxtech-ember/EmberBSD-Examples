#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
: "${HOST_PREFIX:?select the native Ports prefix}" "${OUT:?select an output directory}"
mkdir -p "$OUT"
cd "$OUT"
case "${1:-}" in
quick)
	: "${RV32_QT_PREFIX:?select a Qt Quick installation built for EmberBSD RV32}"
	"$HOST_PREFIX/bin/cmake" -S "$src" -B qt-quick-rv32 -G Ninja \
	    -DCMAKE_MAKE_PROGRAM="$HOST_PREFIX/bin/ninja" \
	    -DCMAKE_TOOLCHAIN_FILE="$RV32_QT_PREFIX/lib/cmake/Qt6/qt.toolchain.cmake" \
	    -DQT_HOST_PATH="$HOST_PREFIX/qt6" -DCMAKE_BUILD_TYPE=MinSizeRel
	"$HOST_PREFIX/bin/cmake" --build qt-quick-rv32 --target weather-quick
	"$HOST_PREFIX/bin/llvm-strip" -o weather-quick qt-quick-rv32/weather-quick
	exit 0
	;;
frame)
	qt="$HOST_PREFIX/qt6"
	"$qt/bin/qmllint" "$src/Weather.qml"
	"$qt/libexec/qmlcachegen" -o Weather.qmlc "$src/Weather.qml"
	QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_SCALE_FACTOR=1 \
	    "$qt/bin/qmltestrunner" -input "$src/tests" -o -,txt
	"$HOST_PREFIX/bin/bmptopnm" weather.bmp >weather.ppm
	"$HOST_PREFIX/bin/python3.15" -B "$src/pack-frame.py" weather.ppm weather-frame.inc
	"$HOST_PREFIX/bin/pnmtopng" weather.ppm >weather.png
	set -- frame -I.
	;;
lvgl)
	: "${LVGL_PREFIX:?select the RV32 LVGL package prefix}"
	set -- lvgl -DLV_CONF_INCLUDE_SIMPLE -DLV_KCONFIG_IGNORE "-I$LVGL_PREFIX/include"
	;;
*) echo 'usage: build.sh quick|frame|lvgl' >&2; exit 2 ;;
esac
variant=$1
shift
: "${SYSROOT:?select the RV32 C sysroot}" "${BUILTINS:?select the RV32 builtins archive}"
: "${UNWIND:?select the RV32 unwind archive}"
cc() {
	"${TARGET_CLANG:-$HOST_PREFIX/bin/clang}" --target=riscv32--netbsd \
	    --sysroot="$SYSROOT" -march=rv32imac -mabi=ilp32 \
	    -Oz -ffunction-sections -fdata-sections "$@"
}
# Each variant can be built independently in the same directory.
cc -Wall -Wextra -Werror -c "$src/weather-display.c" -o "display-$variant.o"
cc -Wall -Wextra -Werror "$@" -c "$src/weather-$variant.c" -o "$variant.o"
set --
if [ "$variant" = lvgl ]; then set -- "$LVGL_PREFIX/lib/liblvgl.a"; fi
lib="$SYSROOT/usr/lib"
cc --ld-path="$HOST_PREFIX/bin/ld.lld" -static -nostdlib -Wl,--gc-sections,-s \
    -o "weather-$variant" "$lib/crt0.o" "$lib/crti.o" "$lib/crtbegin.o" \
    "display-$variant.o" "$variant.o" -Wl,--start-group "$@" "$lib/libc.a" \
    "$BUILTINS" "$UNWIND" -Wl,--end-group "$lib/crtend.o" "$lib/crtn.o"
