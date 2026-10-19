#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted. Use the selected GCC16 application sysroot.
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: CXX=... SYSROOT=... $0 NEW_OUTPUT_DIR" >&2
    exit 2
fi
: "${CXX:?Set CXX to the shared GCC16 cross compiler}"
: "${SYSROOT:?Set SYSROOT to the verified application sysroot}"
case $("$CXX" --version | sed -n '1p') in
    *clang*) echo "GCC16 is required" >&2; exit 2 ;;
    *GCC*) ;;
    *) echo "A GNU GCC16 compiler is required" >&2; exit 2 ;;
esac
case $("$CXX" -dumpfullversion) in
    16.*) ;;
    *) echo "GCC16 is required" >&2; exit 2 ;;
esac
case $("$CXX" -dumpmachine) in
    *netbsd*) ;;
    *) echo "An EmberBSD target compiler is required" >&2; exit 2 ;;
esac
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=$1
[ ! -e "$out" ] && [ ! -L "$out" ] || { echo "output exists: $out" >&2; exit 2; }
mkdir -p "$out"
qt=$SYSROOT/usr/pkg/qt6
"$CXX" --sysroot="$SYSROOT" -std=c++17 -O2 -fPIC -Wall -Wextra -Werror \
    -I"$SYSROOT/usr/pkg/include" -I"$qt/include" -I"$qt/include/QtCore" -I"$qt/include/QtGui" \
    -I"$qt/include/QtQuick" -I"$qt/include/QtQml" -I"$qt/include/QtDBus" \
    -L"$qt/lib" -L"$SYSROOT/usr/pkg/lib" -L"$SYSROOT/usr/pkg/gcc16/lib" \
    -Wl,-rpath-link,"$qt/lib" -Wl,-rpath-link,"$SYSROOT/usr/pkg/lib" \
    -Wl,-rpath-link,"$SYSROOT/usr/pkg/gcc16/lib" \
    -Wl,-rpath,/usr/pkg/qt6/lib -Wl,-rpath,/usr/pkg/gcc16/lib \
    -MMD -MF "$out/frame-check.d" "$src/frame-check.cpp" \
    -lQt6Quick -lQt6DBus -lQt6Gui -lQt6Qml -lQt6Core -o "$out/frame-check"
cp "$src/Quadrants.qml" "$out/Quadrants.qml"
