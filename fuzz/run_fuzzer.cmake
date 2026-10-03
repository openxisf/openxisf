# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The OpenXISF Authors

# Runs a libFuzzer target for SECONDS, starting from its committed SEEDS and its local CORPUS, and fails when it finds
# a crash, a leak or a slow input (written to ARTIFACTS), or when it covers fewer than MIN_COVERAGE edges. The last
# check catches a target that stops early on every input, which would otherwise pass without testing anything.
#
# cmake -D FUZZER=... -D SEEDS=... -D CORPUS=... -D ARTIFACTS=... -D SECONDS=... -D MIN_COVERAGE=... -P run_fuzzer.cmake

foreach(variable FUZZER SEEDS CORPUS ARTIFACTS SECONDS MIN_COVERAGE)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "run_fuzzer.cmake: ${variable} is not set")
    endif()
endforeach()

file(MAKE_DIRECTORY "${CORPUS}" "${ARTIFACTS}")

# New inputs go to the first directory, the local corpus; the seeds are only read.
execute_process(
    COMMAND "${FUZZER}" "-max_total_time=${SECONDS}" -timeout=10 -print_final_stats=1
            "-artifact_prefix=${ARTIFACTS}/" "${CORPUS}" "${SEEDS}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
    ECHO_OUTPUT_VARIABLE
    ECHO_ERROR_VARIABLE)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "The fuzzer failed (${result}). The input that broke it is in ${ARTIFACTS}.")
endif()

# libFuzzer reports the edges it has covered as "cov: N" in its status lines; the last one is the final count.
string(REGEX MATCHALL "cov: [0-9]+" coverage "${output}")
if(NOT coverage)
    message(FATAL_ERROR "The fuzzer did not report its coverage.")
endif()
list(POP_BACK coverage last)
string(REPLACE "cov: " "" edges "${last}")
if(edges LESS MIN_COVERAGE)
    message(FATAL_ERROR "The fuzzer covered ${edges} edges, fewer than the ${MIN_COVERAGE} expected. The target "
                        "probably stops early on every input.")
endif()
get_filename_component(target "${FUZZER}" NAME_WE)
message(STATUS "${target} covered ${edges} edges (at least ${MIN_COVERAGE} expected).")
