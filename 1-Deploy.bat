@echo off

git submodule update --init --recursive

mkdir _Compiler
cd _Compiler
cmake .. -A x64

if %ERRORLEVEL% neq 0 call :ErrorOccured

exit /b 0

:ErrorOccured

echo.
echo %~1
echo.

pause

exit /b 1
