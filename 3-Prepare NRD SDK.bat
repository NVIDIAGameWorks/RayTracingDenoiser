@echo off

rd /q /s "_NRD_SDK"

mkdir "_NRD_SDK"
cd "_NRD_SDK"
copy "..\LICENSE.txt" "."
copy "..\README.md" "."

mkdir "Lib"

mkdir "Lib\Debug"
copy "..\_Build\Debug\NRD.dll" "Lib\Debug"
copy "..\_Build\Debug\NRD.lib" "Lib\Debug"
copy "..\_Build\Debug\NRD.pdb" "Lib\Debug"

mkdir "Lib\Release"
copy "..\_Build\Release\NRD.dll" "Lib\Release"
copy "..\_Build\Release\NRD.lib" "Lib\Release"
copy "..\_Build\Release\NRD.pdb" "Lib\Release"

mkdir "Integration"
copy "..\Integration\*" "Integration"

mkdir "Include"
copy "..\Include\*" "Include"

echo.
set /P M=Do you need the shader source code for a white-box integration? [y/n]
if /I "%M%" neq "y" goto END

mkdir "Shaders"
copy "..\Source\Shaders\*" "Shaders"
copy "..\External\MathLib\*.hlsl" "Shaders"
copy "..\Include\*.hlsl" "Shaders"

:END
exit /b 0