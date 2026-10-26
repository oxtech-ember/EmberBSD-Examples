# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.20)
project(ember-wifi LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(wpa_test wpa_test.cpp)
enable_testing()
add_test(NAME wpa_test COMMAND wpa_test)

find_package(Qt6 COMPONENTS Widgets)
if(Qt6_FOUND)
  find_package(KF6DBusAddons REQUIRED)
  add_executable(ember-wifi main.cpp)
  target_link_libraries(ember-wifi PRIVATE Qt6::Widgets KF6::DBusAddons)
  install(TARGETS ember-wifi DESTINATION bin)
  install(FILES ember-wifi.desktop DESTINATION share/applications)
endif()
