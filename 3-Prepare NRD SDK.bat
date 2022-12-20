@echo off

set NRD_DIR=.
set "copy_shaders="
set "no_copy_shaders="
set "copy_integration="
set "no_copy_integration="

:PARSE
if "%~1"=="" goto :MAIN

if /i "%~1"=="-h" goto :HELP
if /i "%~1"=="--help" goto :HELP

if /i "%~1"=="--shaders"    set "copy_shaders=y"
if /i "%~1"=="--no-shaders" set "no_copy_shaders=y"

if /i "%~1"=="--integration" set "copy_integration=y"
if /i "%~1"=="--no-integration" set "no_copy_integration=y"

shift
goto :PARSE

:MAIN

rd /q /s "_NRD_SDK"

mkdir "_NRD_SDK\Include"
mkdir "_NRD_SDK\Lib\Debug"
mkdir "_NRD_SDK\Lib\Release"
mkdir "_NRD_SDK\Shaders"
mkdir "_NRD_SDK\Shaders\Include"

cd "_NRD_SDK"

copy "..\%NRD_DIR%\Include\*" "Include"
copy "..\_Build\Debug\NRD.dll" "Lib\Debug"
copy "..\_Build\Debug\NRD.lib" "Lib\Debug"
copy "..\_Build\Debug\NRD.pdb" "Lib\Debug"
copy "..\_Build\Release\NRD.dll" "Lib\Release"
copy "..\_Build\Release\NRD.lib" "Lib\Release"
copy "..\%NRD_DIR%\Shaders\Include\NRD.hlsli" "Shaders\Include"
copy "..\%NRD_DIR%\Shaders\Include\NRDEncoding.hlsli" "Shaders\Include"
copy "..\%NRD_DIR%\LICENSE.txt" "."
copy "..\%NRD_DIR%\README.md" "."

echo.
if defined copy_shaders goto :SHADERS
if defined no_copy_shaders goto :PRE_INTEGRATION
set /P M=Do you need the shader source code for a white-box integration? [y/n]
if /I "%M%" neq "y" goto :PRE_INTEGRATION

:SHADERS

mkdir "Shaders"

xcopy "..\%NRD_DIR%\Shaders\" "Shaders" /s /y
copy "..\%NRD_DIR%\External\MathLib\*.hlsli" "Shaders\Source"

:PRE_INTEGRATION

echo.
if defined copy_integration goto :INTEGRATION
if defined no_copy_integration goto :END
set /P M=Do you need NRD integration layer? [y/n]
if /I "%M%" neq "y" goto :END

:INTEGRATION

mkdir "Integration"
copy "..\%NRD_DIR%\Integration\*" "Integration"

cd ..

:END

cd ..
exit /b %errorlevel%

:HELP

echo. -h, --help          show help message
echo. --shaders           copy shaders for a white-box integration
echo. --no-shaders        do not copy shaders for a white-box integration
echo. --integration       copy NRD integration layer
echo. --no-integration    do not copy NRD integration layer

exit
