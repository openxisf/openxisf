# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

# vcpkg triplet for ThreadSanitizer on Linux. ThreadSanitizer sees only the synchronization of instrumented code, so the
# dependencies are instrumented too, oneTBB above all, whose scheduler hands the work of a thread to others. They must
# be built by the compiler of the project, which CC and CXX name, since GCC and Clang have runtimes of their own.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_C_FLAGS "-fsanitize=thread")
set(VCPKG_CXX_FLAGS "-fsanitize=thread")
set(VCPKG_LINKER_FLAGS "-fsanitize=thread")
