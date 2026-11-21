#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
qml_app=spark-workshop
. "$src/../qml-wscons-rv32.sh"

qml_scene "$src/SparkWorkshop.qml" sparkworkshop \
	--header "$qml_generated/sparkworkshop.h" \
	--pngtopam "$HOST_PREFIX/bin/pngtopam"
qml_object "$src/guide.c" "$qml_objs/guide.o"
qml_object "$src/view.c" "$qml_objs/view.o"
qml_object "$src/camera.c" "$qml_objs/camera.o"
qml_object "$src/camera-bmp.c" "$qml_objs/camera-bmp.o"
qml_object "$src/workshop.c" "$qml_objs/workshop.o"
qml_link "$qml_objs/guide.o" "$qml_objs/view.o" \
	"$qml_objs/camera.o" "$qml_objs/camera-bmp.o" "$qml_objs/workshop.o"
