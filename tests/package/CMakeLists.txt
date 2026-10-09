# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

# A consumer of OpenXISF, which finds the library as OPENXISF_CONSUMED_AS says: the installed package through
# find_package (the default), the installed pkg-config file, or the sources of OPENXISF_SOURCE_DIR through FetchContent,
# as a project that is not the top level. The test suite of the main project builds it against a staged install (see
# tests/CMakeLists.txt), and CI builds it against the vcpkg port, through pkg-config and from the sources.

cmake_minimum_required(VERSION 3.25)

project(openxisf_package_test LANGUAGES CXX)

set(OPENXISF_CONSUMED_AS "package" CACHE STRING "How the consumer finds OpenXISF: package, pkg-config or sources")
set(OPENXISF_SOURCE_DIR "" CACHE PATH "The sources of OpenXISF, for OPENXISF_CONSUMED_AS=sources")

add_executable(consumer main.cpp)

if(OPENXISF_CONSUMED_AS STREQUAL "package")
    find_package(openxisf CONFIG REQUIRED)
    target_link_libraries(consumer PRIVATE openxisf::openxisf)
    set(version "${openxisf_VERSION}")
elseif(OPENXISF_CONSUMED_AS STREQUAL "pkg-config")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(openxisf REQUIRED openxisf)
    # A static library defines OPENXISF_STATIC_DEFINE, and needs the libraries of its private requirements, which only
    # the static flags list. Libraries are linked after the objects that need them.
    if(openxisf_CFLAGS MATCHES "OPENXISF_STATIC_DEFINE")
        target_compile_options(consumer PRIVATE ${openxisf_STATIC_CFLAGS})
        target_link_libraries(consumer PRIVATE ${openxisf_STATIC_LDFLAGS})
    else()
        target_compile_options(consumer PRIVATE ${openxisf_CFLAGS})
        target_link_libraries(consumer PRIVATE ${openxisf_LDFLAGS})
    endif()
    target_compile_features(consumer PRIVATE cxx_std_20)
    set(version "${openxisf_VERSION}")
elseif(OPENXISF_CONSUMED_AS STREQUAL "sources")
    include(FetchContent)
    FetchContent_Declare(openxisf SOURCE_DIR "${OPENXISF_SOURCE_DIR}")
    FetchContent_MakeAvailable(openxisf)
    # A project that is not the top level gets the library alone.
    foreach(option
            OPENXISF_BUILD_TESTS OPENXISF_BUILD_SAMPLES OPENXISF_BUILD_BENCHMARKS OPENXISF_BUILD_FUZZERS
            OPENXISF_BUILD_DOCS OPENXISF_INSTALL)
        if(${option})
            message(FATAL_ERROR "${option} is on, in a project that is not the top level")
        endif()
    endforeach()
    target_link_libraries(consumer PRIVATE openxisf::openxisf)
    get_target_property(version openxisf VERSION)
else()
    message(FATAL_ERROR "OPENXISF_CONSUMED_AS is ${OPENXISF_CONSUMED_AS}, not package, pkg-config or sources")
endif()

target_compile_definitions(consumer PRIVATE EXPECTED_VERSION="${version}")
