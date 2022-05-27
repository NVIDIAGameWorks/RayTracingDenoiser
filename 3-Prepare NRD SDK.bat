@echo off

set NRD_DIR=.
set "use_pause=y"
set "copy_shaders="
set "no_copy_shaders="

:PARSE
if "%~1"=="" goto :MAIN

if /i "%~1"=="-h"                goto :HELP
if /i "%~1"=="--help"            goto :HELP

if /i "%~1"=="--no-pause"        set "use_pause="

if /i "%~1"=="--copy-shaders"    set "copy_shaders=y"
if /i "%~1"=="--no-copy-shaders" set "no_copy_shaders=y"

shift
goto :PARSE

:MAIN

rd /q /s "_NRD_SDK"

mkdir "_NRD_SDK\Include"
mkdir "_NRD_SDK\Integration"
mkdir "_NRD_SDK\Lib\Debug"
mkdir "_NRD_SDK\Lib\Release"

cd "_NRD_SDK"

copy "..\%NRD_DIR%\Integration\*" "Integration"
copy "..\%NRD_DIR%\Include\*" "Include"
copy "..\_Build\Debug\NRD.dll" "Lib\Debug"
copy "..\_Build\Debug\NRD.lib" "Lib\Debug"
copy "..\_Build\Debug\NRD.pdb" "Lib\Debug"
copy "..\_Build\Release\NRD.dll" "Lib\Release"
copy "..\_Build\Release\NRD.lib" "Lib\Release"
copy "..\%NRD_DIR%\LICENSE.txt" "."
copy "..\%NRD_DIR%\README.md" "."

echo.
if defined copy_shaders goto :SHADERS
if defined no_copy_shaders goto :END
set /P M=Do you need the shader source code for a white-box integration? [y/n]
if /I "%M%" neq "y" goto END

:SHADERS
mkdir "Shaders"

xcopy "..\%NRD_DIR%\Shaders\" "Shaders" /s
copy "..\%NRD_DIR%\External\MathLib\*.hlsli" "Shaders\Source"

:END

cd ..
if defined use_pause pause
exit

:HELP
echo. -h, --help          show help message
echo. --no-pause          skip pause in the end of script
echo. --copy-shaders      copy shadres for a white-box integration
echo. --no-copy-shaders   don't copy shadres for a white-box integration
exit
