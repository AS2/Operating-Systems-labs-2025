#!/bin/bash
set -e

rm -rf build CMakeFiles CMakeCache.txt cmake_install.cmake
mkdir -p build
cd build
cmake ..
make
cd ..


