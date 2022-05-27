@echo off

set "use_pause=y"

:PARSE
if "%~1"=="" goto :MAIN

if /i "%~1"=="-h"         goto :HELP
if /i "%~1"=="--help"     goto :HELP

if /i "%~1"=="--no-pause" set "use_pause="

shift
goto :PARSE

:MAIN

git submodule update --init --recursive
if %errorlevel% neq 0 exit /b %errorlevel%

mkdir "_Compiler"

cd "_Compiler"
cmake .. -A x64
if %errorlevel% neq 0 exit /b %errorlevel%
cd ..

if defined use_pause pause
exit

:HELP
echo. -h, --help    show help message
echo. --no-pause    skip pause in the end of script
exit
