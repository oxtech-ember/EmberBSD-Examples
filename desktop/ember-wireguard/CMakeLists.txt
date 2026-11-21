# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.20)
project(ember-wireguard LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTING "Build tests" OFF)
include(CTest)
if(BUILD_TESTING)
  add_executable(tunnel_test tunnel_test.cpp)
  add_test(NAME tunnel_test COMMAND tunnel_test)
  add_executable(routes_test routes_test.c routes.c)
  add_test(NAME routes_test COMMAND routes_test)
  add_executable(authorization_test authorization_test.c routes.c)
  add_test(NAME authorization_test COMMAND authorization_test)
  add_executable(security_parse_test security_parse_test.c routes.c)
  add_test(NAME security_parse_test COMMAND security_parse_test)
endif()

add_executable(ember-wireguard-ctl ctl.c routes.c)
# Prefer relative RPATH from the pkgsrc link wrapper; setuid bits are
# applied by the Ports package PLIST @mode.
install(TARGETS ember-wireguard-ctl DESTINATION libexec)

find_package(Qt6 COMPONENTS Widgets)
if(Qt6_FOUND)
  find_package(KF6DBusAddons REQUIRED)
  find_program(EMBER_WIREGUARD_LRELEASE NAMES lrelease
    HINTS "${EMBERBSD_HOST_PREFIX}/qt6/bin" "$ENV{EMBERBSD_HOST_PREFIX}/qt6/bin"
    REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  set(translation_qm "${CMAKE_CURRENT_BINARY_DIR}/ember-wireguard_ru.qm")
  add_custom_command(OUTPUT "${translation_qm}"
    COMMAND "${EMBER_WIREGUARD_LRELEASE}" "${CMAKE_CURRENT_SOURCE_DIR}/ember-wireguard_ru.ts" -qm "${translation_qm}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ember-wireguard_ru.ts" VERBATIM)
  set_source_files_properties("${translation_qm}" PROPERTIES QT_RESOURCE_ALIAS ember-wireguard_ru.qm)
  add_executable(ember-wireguard main.cpp)
  qt_add_resources(ember-wireguard translations PREFIX /translations FILES "${translation_qm}")
  target_link_libraries(ember-wireguard PRIVATE Qt6::Widgets KF6::DBusAddons)
  if(BUILD_TESTING)
    add_executable(config_save_test config_save_test.cpp)
    qt_add_resources(config_save_test translations_test PREFIX /translations FILES "${translation_qm}")
    target_link_libraries(config_save_test PRIVATE Qt6::Widgets KF6::DBusAddons)
    add_test(NAME config_save_test COMMAND config_save_test)
  endif()
  install(TARGETS ember-wireguard DESTINATION bin)
  install(FILES ember-wireguard.desktop DESTINATION share/applications)
endif()
