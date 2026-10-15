# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.22)
project(ember_ncnn_cpu LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Consume only the installed SDK, without source-tree headers.
find_package(ncnn 20260526 EXACT CONFIG REQUIRED)
if(NCNN_VULKAN OR NCNN_OPENMP OR NOT NCNN_THREADS OR NOT NCNN_SHARED_LIB)
    message(FATAL_ERROR "This example requires shared CPU ncnn, NCNN_THREADS=ON, OpenMP=OFF and Vulkan=OFF")
endif()
find_program(RUBY_EXECUTABLE ruby REQUIRED NO_CMAKE_FIND_ROOT_PATH)
set(models "${CMAKE_CURRENT_BINARY_DIR}/models")
set(model_files)
foreach(name tiny-cnn.param tiny-cnn.bin malformed.param truncated.bin nonfinite.bin)
    list(APPEND model_files "${models}/${name}")
endforeach()
add_custom_command(OUTPUT ${model_files}
    COMMAND "${RUBY_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate-model.rb" "${models}"
    DEPENDS generate-model.rb VERBATIM)
add_custom_target(ncnn-cpu-model ALL DEPENDS ${model_files})
add_executable(ncnn-cpu-c inference.c)
add_executable(ncnn-cpu-cxx inference.cpp)
foreach(program ncnn-cpu-c ncnn-cpu-cxx)
    target_compile_options(${program} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(${program} PRIVATE ncnn m)
    add_dependencies(${program} ncnn-cpu-model)
    set_target_properties(${program} PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endforeach()
install(TARGETS ncnn-cpu-c ncnn-cpu-cxx RUNTIME DESTINATION bin)
install(FILES "${models}/tiny-cnn.param" "${models}/tiny-cnn.bin" DESTINATION share/examples/ncnn-cpu)
install(FILES LICENSE DESTINATION share/licenses/ncnn-cpu)

include(CTest)
if(BUILD_TESTING)
    # The reference executable has no ncnn or model-generator dependency.
    add_executable(check-ncnn-csv tests/check-results.cpp)
    add_executable(ncnn-thread-lifecycle tests/thread-lifecycle.cpp)
    add_executable(ncnn-api-errors tests/api-errors.c)
    foreach(program check-ncnn-csv ncnn-thread-lifecycle ncnn-api-errors)
        target_compile_options(${program} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endforeach()
    target_link_libraries(ncnn-thread-lifecycle PRIVATE ncnn)
    target_link_libraries(ncnn-api-errors PRIVATE ncnn)
    add_dependencies(ncnn-thread-lifecycle ncnn-cpu-model)
    add_dependencies(ncnn-api-errors ncnn-cpu-model)
    add_test(NAME ncnn-thread-lifecycle COMMAND ncnn-thread-lifecycle "${models}/tiny-cnn.param" "${models}/tiny-cnn.bin")
    add_test(NAME ncnn-api-errors COMMAND ncnn-api-errors "${models}/malformed.param" "${models}/absent.param"
        "${models}/tiny-cnn.param" "${models}/absent.bin" "${models}/truncated.bin")
    add_test(NAME ncnn-cpu-cli COMMAND "${CMAKE_COMMAND}"
        "-DC_PROGRAM=$<TARGET_FILE:ncnn-cpu-c>"
        "-DCXX_PROGRAM=$<TARGET_FILE:ncnn-cpu-cxx>"
        "-DCHECKER=$<TARGET_FILE:check-ncnn-csv>"
        "-DMODELS=${models}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake")
    set_tests_properties(ncnn-thread-lifecycle ncnn-api-errors ncnn-cpu-cli PROPERTIES TIMEOUT 120)
endif()
