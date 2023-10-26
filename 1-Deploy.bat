@echo off

git submodule update --init --recursive

mkdir "_Build"

cd "_Build"
cmake .. -A x64
cd ..
