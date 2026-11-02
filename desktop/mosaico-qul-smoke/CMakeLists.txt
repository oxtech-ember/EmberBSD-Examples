# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
cmake_minimum_required(VERSION 3.21.1)

project(mosaico_qul_smoke VERSION 0.1.0 LANGUAGES C CXX ASM)

find_package(Qul REQUIRED)

qul_add_target(mosaico_qul_smoke
    QML_PROJECT mosaico_qul_smoke.qmlproject
    GENERATE_ENTRYPOINT
)
app_target_setup_os(mosaico_qul_smoke)
