#!/bin/bash

mkdir -p build

pushd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
popd

pushd khires
make -j$(nproc)
popd
