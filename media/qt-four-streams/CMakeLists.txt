cmake_minimum_required(VERSION 3.22)
project(ember_qt_four_streams VERSION 1.0 LANGUAGES C CXX)

add_executable(i420-fixture i420-fixture.c)
set_target_properties(i420-fixture PROPERTIES
    C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_compile_options(i420-fixture PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)

option(BUILD_VIEWER "Build the Qt viewer as well as the fixture generator" ON)
if(BUILD_VIEWER)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets Multimedia)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libswscale>=10 libavutil)
    add_executable(qt-four-streams main.cpp)
    target_compile_features(qt-four-streams PRIVATE cxx_std_17)
    set_target_properties(qt-four-streams PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    target_compile_options(qt-four-streams PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
    target_link_libraries(qt-four-streams PRIVATE Qt6::Widgets Qt6::Multimedia PkgConfig::FFMPEG)
    set_target_properties(qt-four-streams PROPERTIES
        INSTALL_RPATH "/usr/pkg/qt6/lib;/usr/pkg/lib;/usr/pkg/lib/ffmpeg9;/usr/pkg/gcc16/lib")
    install(TARGETS qt-four-streams RUNTIME DESTINATION bin)
endif()
