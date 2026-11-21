#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
: "${HOST_PREFIX:?set HOST_PREFIX to the selected native Ports prefix}"
: "${EMBER_SDK_SOURCE:?set EMBER_SDK_SOURCE to the SDK checkout}"
: "${EMBER_RUNTIME_SOURCE:?set EMBER_RUNTIME_SOURCE to the Runtime checkout}"
: "${OUT:?set OUT to a separate test output directory}"
QT_HOST_PREFIX=${QT_HOST_PREFIX:-$HOST_PREFIX/qt6}
PYTHON=${PYTHON:-$HOST_PREFIX/bin/python3.15}
CC=${CC:-$HOST_PREFIX/bin/clang}
CXX=${CXX:-$HOST_PREFIX/bin/clang++}
CMAKE=${CMAKE:-$HOST_PREFIX/bin/cmake}
NINJA=${NINJA:-$HOST_PREFIX/bin/ninja}
mkdir -p "$OUT/generated" "$OUT/frames"
"$PYTHON" "$src/check_fixtures.py"
set --
if [ -n "${CMAKE_OSX_SYSROOT:-}" ]; then
    set -- "$@" -DCMAKE_OSX_SYSROOT="$CMAKE_OSX_SYSROOT"
fi
"$CMAKE" -G Ninja -S "$src/qt-reference" -B "$OUT/qt-reference-build" \
    -DCMAKE_PREFIX_PATH="$QT_HOST_PREFIX" \
    -DCMAKE_MAKE_PROGRAM="$NINJA" -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_BUILD_TYPE=MinSizeRel "$@"
"$CMAKE" --build "$OUT/qt-reference-build" --target qt_reference
for scene in visualparent visualparent2 grid-spacing; do
    case "$scene" in
    visualparent) width=320; height=480; symbol=visualparent ;;
    visualparent2) width=320; height=480; symbol=visualparent2 ;;
    grid-spacing) width=112; height=112; symbol=gridspacing ;;
    esac
    generated="$OUT/generated/$scene.c"
    runtime_frame="$OUT/frames/$scene-runtime.rgb565"
    qt_frame="$OUT/frames/$scene-qt.rgb565"
    "$PYTHON" "$EMBER_SDK_SOURCE/scripts/qml-compile.py" \
        "$src/upstream/qtdeclarative-v6.12.0/$scene.qml" \
        --name "$symbol" --output "$generated"
    "$CC" -std=c11 -Wall -Wextra -Werror -I"$EMBER_RUNTIME_SOURCE/qml" \
        -DSCENE_SYMBOL=eqml_"$symbol" "$src/render_generated.c" \
        "$generated" "$EMBER_RUNTIME_SOURCE/qml/eqml.c" -o "$OUT/$scene-render"
    "$OUT/$scene-render" "$runtime_frame" "$width" "$height"
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_SCALE_FACTOR=1 \
        QT_QPA_PLATFORM_PLUGIN_PATH="$QT_HOST_PREFIX/plugins/platforms" \
        QT_PLUGIN_PATH="$QT_HOST_PREFIX/plugins" QML2_IMPORT_PATH="$QT_HOST_PREFIX/qml" \
        "$OUT/qt-reference-build/qt_reference" \
        "$src/upstream/qtdeclarative-v6.12.0/$scene.qml" "$qt_frame" "$width" "$height"
    "$PYTHON" "$src/compare_frames.py" "$scene" "$runtime_frame" "$qt_frame" "$width" "$height"
done

# Qt Quick accepts this complete interactive example. Keep its expected
# compiler rejection separate from pixel-render tests.
mousearea_input="$src/upstream/qtdeclarative-v6.12.0/mousearea.qml"
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_SCALE_FACTOR=1 \
    QT_QPA_PLATFORM_PLUGIN_PATH="$QT_HOST_PREFIX/plugins/platforms" \
    QT_PLUGIN_PATH="$QT_HOST_PREFIX/plugins" QML2_IMPORT_PATH="$QT_HOST_PREFIX/qml" \
    "$OUT/qt-reference-build/qt_reference" "$mousearea_input" \
    "$OUT/frames/mousearea-qt.rgb565" 320 480

negative="$OUT/generated/mousearea.c"
"$PYTHON" -c 'from pathlib import Path; import sys; Path(sys.argv[1]).unlink(missing_ok=True)' "$negative"
if "$PYTHON" "$EMBER_SDK_SOURCE/scripts/qml-compile.py" \
    "$mousearea_input" \
    --name mousearea --output "$negative" >"$OUT/mousearea.stdout" 2>"$OUT/mousearea.stderr"; then
    echo "FAIL: unsupported interactive QML unexpectedly compiled" >&2
    exit 1
fi
"$PYTHON" "$src/check_unsupported.py" "$OUT/mousearea.stderr" "$negative" \
    mousearea.qml "unsupported object or grouped property anchors"
