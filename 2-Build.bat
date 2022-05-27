@echo off

set "use_pause=y"
set "debug_build="
set "no_debug_build="

:PARSE
if "%~1"=="" goto :MAIN

if /i "%~1"=="-h"               goto :HELP
if /i "%~1"=="--help"           goto :HELP

if /i "%~1"=="--no-pause"       set "use_pause="

if /i "%~1"=="--debug-build"    set "debug_build=y"
if /i "%~1"=="--no-debug-build" set "no_debug_build=y"

shift
goto :PARSE

:MAIN
mkdir "_Compiler"

cd "_Compiler"
del CMakeCache.txt
cmake -DCMAKE_BUILD_TYPE=Release .. -A x64
if %errorlevel% neq 0 goto :END
cmake --build . --config Release
if %errorlevel% neq 0 goto :END
cd ..

echo.
if defined debug_build goto :BUILD_DEBUG
if defined no_debug_build goto :END
set /P M=Do you want to build DEBUG configuration? [y/n]
if /I "%M%" neq "y" goto :END

:BUILD_DEBUG

cd "_Compiler"
del CMakeCache.txt
cmake -DCMAKE_BUILD_TYPE=Debug .. -A x64
if %errorlevel% neq 0 goto :END
cmake --build . --config Debug
if %errorlevel% neq 0 goto :END
cd ..

:END
if defined use_pause pause
exit /b %errorlevel%

:HELP
echo. -h, --help            show help message
echo. --no-pause            skip pause in the end of script
echo. --debug-build         build NRD SDK in Debug configuration
echo. --no-debug-build      don't build NRD SDK in Debug configuration
exit
