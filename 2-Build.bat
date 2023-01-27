@echo off

cd "build"
cmake --build . --config Release
cmake --build . --config Debug
cd ..
