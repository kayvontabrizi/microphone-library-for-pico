#!/bin/bash
export PICO_SDK_PATH="$(realpath ../pico-sdk/)"
mkdir -p build
cd build
cmake .. -DPICO_BOARD=pico
make
