@echo off

if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo "ERROR: You need VS2017 version 15.2 or later (for vswhere.exe)"
    pause
    exit /b 1
)

setlocal EnableDelayedExpansion

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
    set VS_PATH=%%i
)

setlocal DisableDelayedExpansion

set VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat
if NOT exist "%VCVARS%" (
    echo "ERROR: Could not find %VCVARS%"
    pause
    exit /b 1
)

call "%VCVARS%"
msbuild /t:Build /p:Configuration=Release /p:Platform=x64 "_Compiler\vs2017\SANDBOX.sln"

pause
