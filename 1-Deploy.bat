@echo off

git submodule update --init --recursive

mkdir "_Compiler"

cd "_Compiler"
cmake .. -A x64
cd ..

pause
