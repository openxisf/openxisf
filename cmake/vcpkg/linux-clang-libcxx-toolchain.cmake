# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

# Chained by the x64-linux-libcxx triplet, both for the dependencies and for the project itself. The
# compiler comes from CC and CXX or from the preset; only the standard library is chosen here.
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-stdlib=libc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-stdlib=libc++")
