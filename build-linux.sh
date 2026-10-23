#!/bin/sh
# Cross-compile the Windows DLLs on Linux (mingw-w64 with GCC 13+, cmake 3.28+, ninja, python3)
set -e
cd "$(dirname "$0")"
git submodule update --init --recursive
cmake -S . -B build-linux -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
