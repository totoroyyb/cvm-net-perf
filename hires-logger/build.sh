#!/bin/bash

mkdir -p build

pushd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
popd

pushd khires
make -j$(nproc)
popd

pushd profiler
cargo build --release
popd
