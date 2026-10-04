# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

# Install rules and the CMake and pkg-config packages. Included by the top-level project.

include(CMakePackageConfigHelpers)

set(OPENXISF_CONFIG_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/openxisf")

install(TARGETS openxisf
    EXPORT openxisf_targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/openxisf" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${PROJECT_BINARY_DIR}/include/openxisf/version.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/openxisf")

if(MSVC AND OPENXISF_SHARED)
    install(FILES "$<TARGET_PDB_FILE:openxisf>" DESTINATION "${CMAKE_INSTALL_BINDIR}" OPTIONAL)
endif()

install(EXPORT openxisf_targets
    NAMESPACE openxisf::
    FILE openxisf-targets.cmake
    DESTINATION "${OPENXISF_CONFIG_DIR}")

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/openxisf-config.cmake.in"
    "${PROJECT_BINARY_DIR}/openxisf-config.cmake"
    INSTALL_DESTINATION "${OPENXISF_CONFIG_DIR}")
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/openxisf-config-version.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY ${OPENXISF_VERSION_COMPATIBILITY})
install(FILES
    "${PROJECT_BINARY_DIR}/openxisf-config.cmake"
    "${PROJECT_BINARY_DIR}/openxisf-config-version.cmake"
    DESTINATION "${OPENXISF_CONFIG_DIR}")
install(FILES
    "${PROJECT_SOURCE_DIR}/cmake/Findlz4.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/Findzstd.cmake"
    DESTINATION "${OPENXISF_CONFIG_DIR}/modules")

# pkg-config. The prefix is relative to the file, so the installed tree can be moved. Any absolute
# directory will do as the root of the computation.
file(RELATIVE_PATH OPENXISF_PC_PREFIX_RELPATH
    "${PROJECT_BINARY_DIR}/prefix/${CMAKE_INSTALL_LIBDIR}/pkgconfig" "${PROJECT_BINARY_DIR}/prefix")
string(REGEX REPLACE "/$" "" OPENXISF_PC_PREFIX_RELPATH "${OPENXISF_PC_PREFIX_RELPATH}")
set(OPENXISF_PC_REQUIRES_PRIVATE "")
set(OPENXISF_PC_CFLAGS "")
if(NOT OPENXISF_SHARED)
    set(requires pugixml zlib liblz4 libzstd)
    if(OPENXISF_WITH_TBB)
        list(APPEND requires tbb)
    endif()
    if(OPENXISF_WITH_OPENSSL)
        list(APPEND requires libcrypto)
    endif()
    list(JOIN requires ", " requires_list)
    set(OPENXISF_PC_REQUIRES_PRIVATE "Requires.private: ${requires_list}")
    set(OPENXISF_PC_CFLAGS " -DOPENXISF_STATIC_DEFINE")
endif()
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/openxisf.pc.in"
    "${PROJECT_BINARY_DIR}/openxisf.pc"
    @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/openxisf.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/NOTICE"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/openxisf")
