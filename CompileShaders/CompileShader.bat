@echo off

set WINSDK_PATH=c:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64

set NAME=%~n3
set DOT_EXT=%~x3
set HEADER=%NAME%_%DOT_EXT:~1,99%

:: DXBC (D3D11)
if "%1" neq "lib" (
    "%WINSDK_PATH%\FXC.exe" /T %1_5_0 /WX /nologo /O3 /E main "%~2" /DCOMPILER_FXC=1 /Vn g_%HEADER%_dxbc /Fo "%~3.dxbc" /Fh "%~4.dxbc.h" /I "%cd%"
)

:: DXIL (D3D12)
if "%1" neq "lib" set entryPoint=-E main
    "..\_Build\HostDeps\DXC\bin\x64\dxc.exe" -T %1_6_3 -WX -nologo -O3 %entryPoint% "%~2" -DCOMPILER_DXC=1 -Vn g_%HEADER%_dxil -Fo "%~3.dxil" -Fh "%~4.dxil.h" -I "%cd%"

:: SPIRV (VULKAN)
set "S_OFFSET=100"
set "T_OFFSET=200"
set "B_OFFSET=300"
set "U_OFFSET=400"

set "S_SHIFT=-fvk-s-shift %S_OFFSET% 0 -fvk-s-shift %S_OFFSET% 1 -fvk-s-shift %S_OFFSET% 2 "
set "T_SHIFT=-fvk-t-shift %T_OFFSET% 0 -fvk-t-shift %T_OFFSET% 1 -fvk-t-shift %T_OFFSET% 2 "
set "B_SHIFT=-fvk-b-shift %B_OFFSET% 0 -fvk-b-shift %B_OFFSET% 1 -fvk-b-shift %B_OFFSET% 2 "
set "U_SHIFT=-fvk-u-shift %U_OFFSET% 0 -fvk-u-shift %U_OFFSET% 1 -fvk-u-shift %U_OFFSET% 2 "

"..\_Build\HostDeps\DXC\bin\x64\dxc.exe" -T %1_6_3 -O3 -WX %S_SHIFT% %T_SHIFT% %B_SHIFT% %U_SHIFT% -D VULKAN -spirv -fspv-target-env=vulkan1.2 -Vn g_%HEADER%_spirv -Fo "%~3.spirv" "%~2" -Fh "%~4.spirv.h" -I "%cd%" 
