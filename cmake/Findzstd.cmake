# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 The OpenXISF Authors

# Provides the imported target zstd::libzstd.
#
# It is looked up as a CMake package first (vcpkg, upstream builds), then through pkg-config, because
# several distributions (Debian, Ubuntu, Homebrew, MSYS2) ship only libzstd.pc.

if(NOT TARGET zstd::libzstd)
    find_package(zstd CONFIG QUIET)

    if(NOT TARGET zstd::libzstd)
        foreach(candidate zstd::libzstd_shared zstd::libzstd_static)
            if(TARGET ${candidate})
                add_library(zstd::libzstd INTERFACE IMPORTED)
                target_link_libraries(zstd::libzstd INTERFACE ${candidate})
                break()
            endif()
        endforeach()
    endif()

    if(NOT TARGET zstd::libzstd)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(openxisf_pc_zstd QUIET IMPORTED_TARGET libzstd)
            if(TARGET PkgConfig::openxisf_pc_zstd)
                add_library(zstd::libzstd INTERFACE IMPORTED)
                target_link_libraries(zstd::libzstd INTERFACE PkgConfig::openxisf_pc_zstd)
            endif()
        endif()
    endif()
endif()

if(TARGET zstd::libzstd)
    set(zstd_target zstd::libzstd)
else()
    set(zstd_target zstd_target-NOTFOUND)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(zstd
    REQUIRED_VARS zstd_target
    FAIL_MESSAGE "Could not find zstd: install its CMake package or libzstd.pc, and pkg-config if needed")
