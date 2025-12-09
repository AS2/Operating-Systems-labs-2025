#!/usr/bin/sh

set -xe

cmake -Bbuild && cd ./build && make
cd ..
cp ./build/host .
rm -rf ./build
