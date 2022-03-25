@echo off

set NRD_DIR=.

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
set /P M=Do you need the shader source code for a white-box integration? [y/n]
if /I "%M%" neq "y" goto END

mkdir "Shaders"

xcopy "..\%NRD_DIR%\Shaders\" "Shaders" /s
copy "..\%NRD_DIR%\External\MathLib\*.hlsli" "Shaders\Source"

:END

cd ..
