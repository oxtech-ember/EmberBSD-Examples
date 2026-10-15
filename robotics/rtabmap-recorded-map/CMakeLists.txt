# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.22)
project(ember_rtabmap_recorded_map LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Eigen3 5.0.1 EXACT CONFIG REQUIRED)
find_package(OpenCV 5.0.0 EXACT REQUIRED COMPONENTS core imgproc imgcodecs features calib geometry video photo stitching videoio)
find_package(PCL 1.15.1 EXACT CONFIG REQUIRED)
find_package(GTSAM 4.3.0 EXACT CONFIG REQUIRED)
find_package(PNG 1.6.59 EXACT REQUIRED)
find_package(SQLite3 3.54.0 EXACT REQUIRED)
find_package(RTABMap 0.23.8 EXACT CONFIG REQUIRED COMPONENTS core utilite)

add_library(recorded-input STATIC recording.cpp)
target_include_directories(recorded-input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(recorded-input PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(ember-rtabmap-recorded-map main.cpp)
target_compile_options(ember-rtabmap-recorded-map PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(ember-rtabmap-recorded-map PRIVATE recorded-input rtabmap::core opencv_imgcodecs SQLite::SQLite3)
add_executable(check-recorded-map tests/check-map.cpp png-reader.c)
target_compile_options(check-recorded-map PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(check-recorded-map PRIVATE recorded-input PNG::PNG SQLite::SQLite3)
install(TARGETS ember-rtabmap-recorded-map check-recorded-map RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    if(NOT IS_ABSOLUTE "${RECORDED_FIXTURE}" OR NOT EXISTS "${RECORDED_FIXTURE}/frames.tsv")
        message(FATAL_ERROR "Set RECORDED_FIXTURE to the absolute extracted dataset fixture")
    endif()
    add_test(NAME recorded-map-cli COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:ember-rtabmap-recorded-map>"
        "-DCHECKER=$<TARGET_FILE:check-recorded-map>"
        "-DFIXTURE=${RECORDED_FIXTURE}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake")
    set_tests_properties(recorded-map-cli PROPERTIES TIMEOUT 300)
endif()
