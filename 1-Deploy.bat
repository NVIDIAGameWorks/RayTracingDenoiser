@echo off

git submodule update --init --recursive

mkdir "build"

cd "build"
cmake .. -A x64
cd ..
