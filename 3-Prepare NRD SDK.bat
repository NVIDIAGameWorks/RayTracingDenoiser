@echo off

set NRD_DIR=.

rd /q /s "_NRD_SDK"

mkdir "_NRD_SDK"
pushd "_NRD_SDK"

copy "..\%NRD_DIR%\LICENSE.txt" "."
copy "..\%NRD_DIR%\README.md" "."

mkdir "Integration"
copy "..\%NRD_DIR%\Integration\*" "Integration"

mkdir "Include"
copy "..\%NRD_DIR%\Include\*" "Include"

mkdir "Lib"
mkdir "Lib\Debug"
copy "..\_Build\Debug\NRD.dll" "Lib\Debug"
copy "..\_Build\Debug\NRD.lib" "Lib\Debug"
copy "..\_Build\Debug\NRD.pdb" "Lib\Debug"
mkdir "Lib\Release"
copy "..\_Build\Release\NRD.dll" "Lib\Release"
copy "..\_Build\Release\NRD.lib" "Lib\Release"

echo.
set /P M=Do you need the shader source code for a white-box integration? [y/n]
if /I "%M%" neq "y" goto END

mkdir "Shaders"
copy "..\%NRD_DIR%\Source\Shaders\*" "Shaders"
copy "..\%NRD_DIR%\Source\Shaders\Include\*" "Shaders"
copy "..\%NRD_DIR%\External\MathLib\*.hlsli" "Shaders"
copy "..\%NRD_DIR%\Include\*.hlsli" "Shaders"

:END
popd

exit /b 0