#!/bin/bash -eu
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

# The build script of OpenXISF for OSS-Fuzz. It runs in the image of the openxisf project of the OSS-Fuzz repository,
# whose Dockerfile clones this repository and the sources of its dependencies into $SRC, with the compiler, the flags
# and the fuzzing engine of the build in $CC, $CXX, $CFLAGS, $CXXFLAGS and $LIB_FUZZING_ENGINE. The dependencies are
# built from their sources with the same flags, so that every sanitizer sees them, and linked statically. Each fuzz
# target goes to $OUT with its seed corpus. No CMake project gets a build type, whose flags would come after those of
# $CXXFLAGS, which OSS-Fuzz chooses for each sanitizer.

prefix="$WORK/prefix"
mkdir -p "$prefix"
jobs="$(nproc)"

# zlib
(cd "$SRC/zlib" && ./configure --static --prefix="$prefix" && make -j"$jobs" install)

# LZ4
(cd "$SRC/lz4" && make -j"$jobs" -C lib install PREFIX="$prefix" BUILD_SHARED=no)

# Zstandard
(cd "$SRC/zstd" && make -j"$jobs" -C lib install-static install-includes install-pc PREFIX="$prefix")

# pugixml
cmake -S "$SRC/pugixml" -B "$WORK/pugixml" -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$WORK/pugixml" -j"$jobs" --target install

# OpenXISF and its fuzz targets. CMake takes the compilers and the flags from the environment.
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig"
cmake -S "$SRC/openxisf" -B "$WORK/openxisf" -DCMAKE_PREFIX_PATH="$prefix" \
    -DOPENXISF_BUILD_TESTS=OFF -DOPENXISF_BUILD_SAMPLES=OFF -DOPENXISF_INSTALL=OFF -DOPENXISF_BUILD_FUZZERS=ON \
    "-DOPENXISF_FUZZING_ENGINE=$LIB_FUZZING_ENGINE"
cmake --build "$WORK/openxisf" -j"$jobs"

for target in text_grammar header block_attributes codecs property_text xisb_index astrometry roundtrip; do
    cp "$WORK/openxisf/bin/fuzz_$target" "$OUT/"
    (cd "$SRC/openxisf/fuzz/seeds/$target" && zip -q -r "$OUT/fuzz_${target}_seed_corpus.zip" .)
done
