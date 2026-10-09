# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

# lz4 and zstd, the private dependencies of a static library that come with find modules, for openxisf-config.cmake,
# which includes this file with the modules in the module path. find_dependency() returns from this file when a package
# is missing, so that the configuration file restores the module path before it stops.

find_dependency(lz4)
find_dependency(zstd)
set(openxisf_dependencies_found TRUE)
