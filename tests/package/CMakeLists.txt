# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The OpenXISF Authors

# A consumer of the installed package. The test suite of the main project builds it against a staged
# install (see tests/CMakeLists.txt), and CI builds it against the vcpkg port.

cmake_minimum_required(VERSION 3.25)

project(openxisf_package_test LANGUAGES CXX)

find_package(openxisf CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE openxisf::openxisf)
target_compile_definitions(consumer PRIVATE EXPECTED_VERSION="${openxisf_VERSION}")
