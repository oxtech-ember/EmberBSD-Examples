# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.22)
project(ember_gtsam_graph LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Eigen3 5.0.1 EXACT CONFIG REQUIRED)
find_package(GTSAM 4.3.0 EXACT CONFIG REQUIRED)
add_executable(ember-gtsam-graph main.cpp)
target_compile_options(ember-gtsam-graph PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(ember-gtsam-graph PRIVATE gtsam Eigen3::Eigen)
install(TARGETS ember-gtsam-graph RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(check-gtsam-csv tests/check-csv.cpp)
    target_compile_options(check-gtsam-csv PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME graph-cli COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:ember-gtsam-graph>"
        "-DCHECKER=$<TARGET_FILE:check-gtsam-csv>"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake")
    set_tests_properties(graph-cli PROPERTIES TIMEOUT 120)
endif()
