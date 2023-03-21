@echo off

cd "_Compiler"
cmake --build . --config Release -j 4
cmake --build . --config Debug -j 4
cd ..
