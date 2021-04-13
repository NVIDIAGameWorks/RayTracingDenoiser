@echo off

if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo "ERROR: You need VS2017 version 15.2 or later (for vswhere.exe)"
    pause
    exit /b 1
)

set TARGET_VS=vs2017
set VER_VS=^[15.0^^,16.0^^)
if exist "_Compiler\vs2019" (
    set TARGET_VS=vs2019
    set VER_VS=^[16.0^^,17.0^^)
)

setlocal EnableDelayedExpansion

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -version %VER_VS% -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
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

msbuild /t:Build /p:Configuration=Release /p:Platform=x64 "_Compiler\%TARGET_VS%\SANDBOX.sln"

echo.
set /P M=Do you want to build DEBUG configuration? [y/n]
if /I "%M%" neq "y" goto END

:BUILD_DEBUG
msbuild /t:Build /p:Configuration=Debug /p:Platform=x64 "_Compiler\%TARGET_VS%\SANDBOX.sln"

:END
pause
