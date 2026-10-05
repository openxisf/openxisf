# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

# The sample programs. They use the public API only, like an application, and each one runs as a test with the label
# example. The arguments after the name are the arguments of its test.

function(openxisf_add_sample name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE openxisf::openxisf)
    set_target_properties(${name} PROPERTIES
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
    # Windows: the UTF-8 arguments of arguments.h come from CommandLineToArgvW() of shell32, and windows.h comes without
    # the min and max macros.
    if(WIN32)
        target_compile_definitions(${name} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(${name} PRIVATE shell32)
    endif()
    openxisf_enable_warnings(${name})
    openxisf_enable_sanitizers(${name})
    if(OPENXISF_HARDENING)
        openxisf_enable_hardening(${name})
    endif()

    add_test(NAME example.${name} COMMAND ${name} ${ARGN})
    set_tests_properties(example.${name} PROPERTIES LABELS example)
endfunction()

set(sample_data "${PROJECT_SOURCE_DIR}/tests/data/pixinsight")

openxisf_add_sample(custom_io)
openxisf_add_sample(distributed "${CMAKE_CURRENT_BINARY_DIR}/distributed.xish")
openxisf_add_sample(preview "${sample_data}/d1-rich-rgb.xisf" "${CMAKE_CURRENT_BINARY_DIR}/preview.ppm")
openxisf_add_sample(read_info "${sample_data}/d1-rich-rgb.xisf")
add_test(NAME example.read_info.astrometry COMMAND read_info "${sample_data}/d5-astrometry.xisf")
set_tests_properties(example.read_info.astrometry PROPERTIES LABELS example PASS_REGULAR_EXPRESSION
    "astrometric solution 1[.]0 [(]Gnomonic, distortion model layer[)], centre at RA 83[.]82")
openxisf_add_sample(read_pixels "${sample_data}/a4-rgb-f32.xisf")
openxisf_add_sample(stream_io "${sample_data}/a6-rgba-u16.xisf")
openxisf_add_sample(write_image "${CMAKE_CURRENT_BINARY_DIR}/write_image.xisf")
