# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.21.1)
project(mosaico_weather LANGUAGES CXX)

option(WEATHER_USE_QUL "Use a Qt for MCUs platform SDK" OFF)
if(WEATHER_USE_QUL)
    enable_language(C ASM)
    find_package(Qul REQUIRED)
    qul_add_target(weather-qul QML_PROJECT Weather.qmlproject GENERATE_ENTRYPOINT)
    app_target_setup_os(weather-qul)
else()
    find_package(Qt6 REQUIRED COMPONENTS Quick)
    qt_standard_project_setup(REQUIRES 6.7)
    qt_add_executable(weather-quick weather-quick.cpp)
    qt_add_qml_module(weather-quick
        URI Ember.Weather
        VERSION 1.0
        QML_FILES Weather.qml
    )
    target_link_libraries(weather-quick PRIVATE Qt6::Quick)
    # The scene draws rectangles and text without loading external images.
    qt_import_plugins(weather-quick EXCLUDE_BY_TYPE imageformats)
endif()
