# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

# Fuzz targets. When OPENXISF_SANITIZE holds "fuzzer" (Clang), each one is a libFuzzer program, and its test fuzzes for
# OPENXISF_FUZZ_SECONDS from the committed seeds in seeds/<name>. Otherwise it is linked with a replay driver, and its
# test runs the seeds once, so that the regression corpus runs on every platform. Both kinds of test have the label
# fuzz.

set(OPENXISF_FUZZ_SECONDS 60 CACHE STRING "How long the test of each libFuzzer target fuzzes, in seconds")

if("fuzzer" IN_LIST OPENXISF_SANITIZE)
    set(libfuzzer ON)
else()
    set(libfuzzer OFF)
    add_library(openxisf_fuzz_replay OBJECT replay.cpp)
    set_target_properties(openxisf_fuzz_replay PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_features(openxisf_fuzz_replay PRIVATE cxx_std_20)
    openxisf_enable_warnings(openxisf_fuzz_replay)
    openxisf_enable_sanitizers(openxisf_fuzz_replay)
    if(OPENXISF_HARDENING)
        openxisf_enable_hardening(openxisf_fuzz_replay)
    endif()
endif()

# MIN_COVERAGE is the number of edges below which a libFuzzer run counts as broken. It is set well below what the seeds
# alone reach, so that only a target that stops early falls below it.
function(openxisf_add_fuzz_target name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "MIN_COVERAGE" "SOURCES")
    string(REGEX REPLACE "^fuzz_" "" short_name "${name}")
    set(seeds "${CMAKE_CURRENT_SOURCE_DIR}/seeds/${short_name}")

    add_executable(${name} ${arg_SOURCES})
    target_link_libraries(${name} PRIVATE openxisf_internal)
    set_target_properties(${name} PROPERTIES
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
    openxisf_enable_warnings(${name})
    openxisf_enable_sanitizers(${name})
    if(OPENXISF_HARDENING)
        openxisf_enable_hardening(${name})
    endif()

    if(libfuzzer)
        # The sanitizer flags give every target the instrumentation; this links the libFuzzer main program.
        target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
        target_link_options(${name} PRIVATE -fsanitize=fuzzer)
        add_test(NAME fuzz.${short_name}
            COMMAND "${CMAKE_COMMAND}"
                -D "FUZZER=$<TARGET_FILE:${name}>"
                -D "SEEDS=${seeds}"
                -D "CORPUS=${PROJECT_SOURCE_DIR}/fuzz/corpus/${short_name}"
                -D "ARTIFACTS=${CMAKE_CURRENT_BINARY_DIR}/artifacts/${short_name}"
                -D "SECONDS=${OPENXISF_FUZZ_SECONDS}"
                -D "MIN_COVERAGE=${arg_MIN_COVERAGE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/run_fuzzer.cmake")
    else()
        target_link_libraries(${name} PRIVATE openxisf_fuzz_replay)
        add_test(NAME fuzz.${short_name} COMMAND ${name} "${seeds}")
    endif()
    set_tests_properties(fuzz.${short_name} PROPERTIES LABELS fuzz)
endfunction()

openxisf_add_fuzz_target(fuzz_text_grammar SOURCES text_grammar.cpp MIN_COVERAGE 300)
openxisf_add_fuzz_target(fuzz_header SOURCES header.cpp MIN_COVERAGE 400)
openxisf_add_fuzz_target(fuzz_block_attributes SOURCES block_attributes.cpp MIN_COVERAGE 400)
openxisf_add_fuzz_target(fuzz_codecs SOURCES codecs.cpp MIN_COVERAGE 150)
openxisf_add_fuzz_target(fuzz_property_text SOURCES property_text.cpp MIN_COVERAGE 400)
