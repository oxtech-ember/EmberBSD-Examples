#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
qml_app=network-lab
. "$src/../qml-wscons-rv32.sh"

qml_scene "$src/NetworkLab.qml" networklab --header "$qml_generated/networklab.h" \
    --pngtopam "$HOST_PREFIX/bin/pngtopam"
qml_object "$src/network-lab.c" "$qml_objs/app.o"
qml_object "$src/sample.c" "$qml_objs/sample.o"
qml_link "$qml_objs/app.o" "$qml_objs/sample.o"
