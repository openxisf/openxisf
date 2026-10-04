# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

# Provides the imported target lz4::lz4.
#
# It is looked up as a CMake package first (vcpkg, upstream builds), then through pkg-config, because
# several distributions (Debian, Ubuntu, Homebrew, MSYS2) ship only liblz4.pc.

if(NOT TARGET lz4::lz4)
    find_package(lz4 CONFIG QUIET)

    if(NOT TARGET lz4::lz4)
        foreach(candidate LZ4::lz4 LZ4::lz4_shared LZ4::lz4_static)
            if(TARGET ${candidate})
                add_library(lz4::lz4 INTERFACE IMPORTED)
                target_link_libraries(lz4::lz4 INTERFACE ${candidate})
                break()
            endif()
        endforeach()
    endif()

    if(NOT TARGET lz4::lz4)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(openxisf_pc_lz4 QUIET IMPORTED_TARGET liblz4)
            if(TARGET PkgConfig::openxisf_pc_lz4)
                add_library(lz4::lz4 INTERFACE IMPORTED)
                target_link_libraries(lz4::lz4 INTERFACE PkgConfig::openxisf_pc_lz4)
            endif()
        endif()
    endif()
endif()

if(TARGET lz4::lz4)
    set(lz4_target lz4::lz4)
else()
    set(lz4_target lz4_target-NOTFOUND)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(lz4
    REQUIRED_VARS lz4_target
    FAIL_MESSAGE "Could not find lz4: install its CMake package or liblz4.pc, and pkg-config if needed")
