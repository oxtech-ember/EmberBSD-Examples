# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.22)
project(ember_onnx_cpu LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Consume the installed package. Do not add ONNX Runtime source directories.
find_package(onnxruntime 1.31.0 EXACT CONFIG REQUIRED)
find_program(RUBY_EXECUTABLE ruby REQUIRED NO_CMAKE_FIND_ROOT_PATH)
set(model "${CMAKE_CURRENT_BINARY_DIR}/models/tiny-mlp.onnx")
add_custom_command(OUTPUT "${model}" "${CMAKE_CURRENT_BINARY_DIR}/models/malformed.onnx"
    COMMAND "${RUBY_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate-model.rb"
        "${CMAKE_CURRENT_BINARY_DIR}/models"
    DEPENDS generate-model.rb VERBATIM)
add_custom_target(onnx-cpu-model ALL DEPENDS "${model}")
add_executable(onnx-cpu-c inference.c)
add_executable(onnx-cpu-cxx inference.cpp)
foreach(program onnx-cpu-c onnx-cpu-cxx)
    target_compile_options(${program} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(${program} PRIVATE onnxruntime::onnxruntime)
    set_target_properties(${program} PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endforeach()
install(TARGETS onnx-cpu-c onnx-cpu-cxx RUNTIME DESTINATION bin)
install(FILES "${model}" DESTINATION share/examples/onnx-cpu)
install(FILES LICENSE DESTINATION share/licenses/onnx-cpu)

include(CTest)
if(BUILD_TESTING)
    # This checker does not include or link ONNX Runtime.
    add_executable(check-onnx-csv tests/check-results.cpp)
    target_compile_options(check-onnx-csv PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_executable(onnx-thread-lifecycle tests/thread-lifecycle.cpp)
    target_compile_options(onnx-thread-lifecycle PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(onnx-thread-lifecycle PRIVATE onnxruntime::onnxruntime)
    add_dependencies(onnx-thread-lifecycle onnx-cpu-model)
    add_test(NAME onnx-thread-lifecycle COMMAND onnx-thread-lifecycle "${model}")
    add_test(NAME onnx-cpu-cli COMMAND "${CMAKE_COMMAND}"
        "-DC_PROGRAM=$<TARGET_FILE:onnx-cpu-c>"
        "-DCXX_PROGRAM=$<TARGET_FILE:onnx-cpu-cxx>"
        "-DCHECKER=$<TARGET_FILE:check-onnx-csv>"
        "-DMODELS=${CMAKE_CURRENT_BINARY_DIR}/models"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake")
    set_tests_properties(onnx-cpu-cli onnx-thread-lifecycle PROPERTIES TIMEOUT 120)
endif()
