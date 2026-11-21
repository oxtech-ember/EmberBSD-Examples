#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu

src=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
scene=${1:-mousearea}
input=$src/upstream/qtdeclarative-v6.12.0/$scene.qml
set --
case "$scene" in
mousearea|behavior) symbol=$scene ;;
visual-opacity) symbol=visualopacity ;;
dial) symbol=dial; input=$src/dial.qml; set -- --pngtopam "$HOST_PREFIX/bin/pngtopam" ;;
*) echo "usage: $0 [mousearea|behavior|visual-opacity|dial]" >&2; exit 2 ;;
esac

qml_app=$scene
. "$src/../qml-wscons-rv32.sh"
qml_scene "$input" "$symbol" "$@"
qml_object "$src/device/mousearea.c" "$qml_objs/app.o" \
    -DSCENE_SYMBOL=eqml_"$symbol" -DSCENE_NAME="\"$scene\""
qml_link "$qml_objs/app.o"
