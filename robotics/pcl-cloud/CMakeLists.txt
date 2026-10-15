# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.22)
project(ember_pcl_cloud LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Eigen3 5.0.1 EXACT CONFIG REQUIRED)
find_package(PCL 1.15.1 EXACT CONFIG REQUIRED COMPONENTS common filters registration)
add_executable(ember-pcl-cloud main.cpp)
target_compile_options(ember-pcl-cloud PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(ember-pcl-cloud PRIVATE pcl_filters pcl_registration Eigen3::Eigen)
install(TARGETS ember-pcl-cloud RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(check-pcl-csv tests/check-csv.cpp)
    target_compile_options(check-pcl-csv PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME cloud-cli COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:ember-pcl-cloud>"
        "-DCHECKER=$<TARGET_FILE:check-pcl-csv>"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake")
    set_tests_properties(cloud-cli PROPERTIES TIMEOUT 120)
endif()
