#!/bin/sh

mkdir -p "_Compiler"

cd "_Compiler"
cmake ..
cmake --build . --config Release -j 4
cmake --build . --config Debug -j 4
cd ..
