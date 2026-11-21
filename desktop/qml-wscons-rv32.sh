# SPDX-License-Identifier: BSD-2-Clause
# Shared by the interactive QML example builders. The caller sets qml_app to
# the executable name and supplies the normal static RV32 build environment.
: "${qml_app:?set the output executable name}" "${OUT:?set the output directory}"
: "${HOST_PREFIX:?set the native Ports prefix}" "${SYSROOT:?set the RV32 sysroot}"
: "${BUILTINS:?set the RV32 compiler builtins archive}" "${UNWIND:?set the RV32 unwind archive}"
: "${EMBER_SDK_SOURCE:?set the SDK source}" "${EMBER_RUNTIME_SOURCE:?set the Runtime source}"

qml_runtime=$EMBER_RUNTIME_SOURCE/qml
qml_objs=$OUT/obj/$qml_app
qml_generated=$OUT/generated
mkdir -p "$qml_objs" "$qml_generated"

qml_cc()
{
	"${TARGET_CLANG:-$HOST_PREFIX/bin/clang}" --target=riscv32--netbsd \
	    --sysroot="$SYSROOT" -march=rv32imac -mabi=ilp32 -Oz \
	    -ffunction-sections -fdata-sections "$@"
}

# Source, output object, then any application-specific compiler flags.
qml_object()
{
	qml_unit_source=$1
	qml_unit_object=$2
	shift 2
	qml_cc -std=c11 -Wall -Wextra -Werror -I"$qml_runtime" -I"$qml_generated" \
	    "$@" -c "$qml_unit_source" -o "$qml_unit_object"
}

# Source, exported symbol suffix, then optional SDK compiler arguments.
qml_scene()
{
	qml_source=$1
	qml_symbol=$2
	shift 2
	"${PYTHON:-$HOST_PREFIX/bin/python3.15}" "$EMBER_SDK_SOURCE/scripts/qml-compile.py" \
	    "$qml_source" --name "$qml_symbol" -o "$qml_generated/$qml_app.c" "$@"
	for unit in eqml eqml_input eqml_wsdisplay eqml_wsmouse; do
		qml_object "$qml_runtime/$unit.c" "$qml_objs/$unit.o"
	done
	qml_object "$qml_generated/$qml_app.c" "$qml_objs/scene.o"
}

# Link the application's object files with the scene and wscons Runtime.
qml_link()
{
	qml_lib=$SYSROOT/usr/lib
	qml_cc --ld-path="$HOST_PREFIX/bin/ld.lld" -static -nostdlib \
	    -Wl,--gc-sections,-s -o "$OUT/$qml_app" \
	    "$qml_lib/crt0.o" "$qml_lib/crti.o" "$qml_lib/crtbegin.o" "$@" \
	    -Wl,--start-group "$qml_objs/eqml.o" "$qml_objs/eqml_input.o" \
	    "$qml_objs/eqml_wsdisplay.o" "$qml_objs/eqml_wsmouse.o" \
	    "$qml_objs/scene.o" "$qml_lib/libc.a" "$BUILTINS" "$UNWIND" \
	    -Wl,--end-group "$qml_lib/crtend.o" "$qml_lib/crtn.o"
}
