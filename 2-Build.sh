#!/bin/sh

mkdir -p "_Build"

cd "_Build"
cmake ..
cmake --build . --config Release -j 4
cmake --build . --config Debug -j 4
cd ..
