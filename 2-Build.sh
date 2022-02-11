#!/bin/sh

mkdir -p "_Compiler"

cd "_Compiler"
rm CMakeCache.txt
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
cd ..
