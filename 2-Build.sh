#!/bin/sh

mkdir -p "build"

cd "build"
cmake ..
cmake --build . --config Release
cmake --build . --config Debug
cd ..
