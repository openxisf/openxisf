# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The OpenXISF Authors

# Applies OPENXISF_SANITIZE ("address;undefined", "thread" or "fuzzer;address;undefined") to a target. Flags are
# PRIVATE, and a static library does not carry them to its consumers, so every executable calls this itself.

function(openxisf_enable_sanitizers target)
    if(NOT OPENXISF_SANITIZE)
        return()
    endif()

    if("thread" IN_LIST OPENXISF_SANITIZE AND "address" IN_LIST OPENXISF_SANITIZE)
        message(FATAL_ERROR "OPENXISF_SANITIZE: 'thread' cannot be combined with 'address'")
    endif()
    if("fuzzer" IN_LIST OPENXISF_SANITIZE AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "OPENXISF_SANITIZE: 'fuzzer' needs Clang, which provides libFuzzer")
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # cl supports AddressSanitizer only. It does not work with the /RTC checks of the Debug
        # configuration, so use RelWithDebInfo (the msvc-asan preset does).
        if(NOT OPENXISF_SANITIZE STREQUAL "address")
            message(FATAL_ERROR "OPENXISF_SANITIZE: cl supports only 'address', got '${OPENXISF_SANITIZE}'")
        endif()
        target_compile_options(${target} PRIVATE /fsanitize=address)
        target_link_options(${target} PRIVATE /INCREMENTAL:NO)
        return()
    endif()
    if(MSVC)
        # CMake links clang-cl objects with lld-link or link, which ignore -fsanitize and miss the runtime libraries.
        # The clang++ driver adds them, also when it targets the MSVC ABI.
        message(FATAL_ERROR "OPENXISF_SANITIZE does not support clang-cl. Use cl (the msvc-asan preset) or clang++.")
    endif()

    # With "fuzzer", every target gets the coverage instrumentation of libFuzzer; only the fuzz targets link its main
    # program (fuzz/CMakeLists.txt).
    set(kinds ${OPENXISF_SANITIZE})
    list(TRANSFORM kinds REPLACE "^fuzzer$" "fuzzer-no-link")
    list(JOIN kinds "," kinds)
    target_compile_options(${target} PRIVATE -fsanitize=${kinds} -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=${kinds})
    if("undefined" IN_LIST OPENXISF_SANITIZE)
        # A finding must fail the test instead of printing a line and carrying on.
        target_compile_options(${target} PRIVATE -fno-sanitize-recover=all)
    endif()
endfunction()
