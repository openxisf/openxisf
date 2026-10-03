# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The OpenXISF Authors

find_package(benchmark CONFIG REQUIRED)

add_executable(openxisf_benchmarks codecs.cpp)
target_link_libraries(openxisf_benchmarks PRIVATE openxisf_internal benchmark::benchmark)
set_target_properties(openxisf_benchmarks PROPERTIES
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
openxisf_enable_warnings(openxisf_benchmarks)
if(OPENXISF_HARDENING)
    openxisf_enable_hardening(openxisf_benchmarks)
endif()

# One short run of each benchmark, so that they keep working. Its numbers mean nothing; run the program itself, in a
# release build, for real ones.
add_test(NAME benchmarks.smoke COMMAND openxisf_benchmarks --benchmark_min_time=0.001s)
set_tests_properties(benchmarks.smoke PROPERTIES LABELS benchmark)
