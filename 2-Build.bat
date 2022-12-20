@echo off

cd "_Compiler"
cmake --build . --config Release
cmake --build . --config Debug
cd ..
