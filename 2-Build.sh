#!/bin/sh

mkdir -p "_Compiler"

cd "_Compiler"
cmake ..
cmake --build . --config Release
cmake --build . --config Debug
cd ..
