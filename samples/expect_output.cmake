# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

# The test of a sample program and of what it prints: it runs PROGRAM with the argument ARGUMENT, and passes when the
# program exits with 0 and its standard output matches the regular expression EXPECTED. A test with
# PASS_REGULAR_EXPRESSION alone ignores the exit code, so it would pass a program that printed the output and failed.
#
#   cmake -DPROGRAM=<program> -DARGUMENT=<argument> -DEXPECTED=<regular expression> -P expect_output.cmake

execute_process(
    COMMAND "${PROGRAM}" "${ARGUMENT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)
message("${output}${errors}")
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "${PROGRAM} failed: ${result}")
endif()
if(NOT output MATCHES "${EXPECTED}")
    message(FATAL_ERROR "The output of ${PROGRAM} does not match \"${EXPECTED}\"")
endif()
