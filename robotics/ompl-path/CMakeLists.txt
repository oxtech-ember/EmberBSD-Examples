# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.22)
project(ember_ompl_path LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Eigen3 5.0.1 EXACT CONFIG REQUIRED)
find_package(Boost 1.92.0 EXACT CONFIG REQUIRED COMPONENTS serialization program_options)
find_package(ompl 2.0.2 EXACT CONFIG REQUIRED)

add_executable(ember-ompl-path path.cpp)
target_compile_features(ember-ompl-path PRIVATE cxx_std_17)
target_compile_options(ember-ompl-path PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(ember-ompl-path PRIVATE ompl::ompl)
install(TARGETS ember-ompl-path RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(test-path-geometry tests/geometry.cpp)
    target_compile_features(test-path-geometry PRIVATE cxx_std_17)
    target_compile_options(test-path-geometry PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME path-geometry COMMAND test-path-geometry)
    add_test(NAME path-cli COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:ember-ompl-path>"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake")
    set_tests_properties(path-cli PROPERTIES TIMEOUT 600)
endif()
