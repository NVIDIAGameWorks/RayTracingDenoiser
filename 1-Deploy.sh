#!/bin/sh

chmod +x "2-Build.sh"
chmod +x "3-Prepare NRD SDK.sh"
chmod +x "4-Clean.sh"

git submodule update --init --recursive

mkdir -p "_Compiler"

cd "_Compiler"
cmake ..
cd ..
