@echo off

set FRAME_NUM=100

set TARGET_VS=vs2017
if exist "_Compiler\vs2019" set TARGET_VS=vs2019

set DIR=_Build\%TARGET_VS%\Bin\Release\

if not exist %DIR% (
    set DIR=_Build\%TARGET_VS%\Bin\Debug\
)

if not exist %DIR% (
    echo The project is not compiled!
    pause
    exit /b
)

pushd %DIR%

call :TestSample 00_Clear
call :TestSample 01_Triangle
call :TestSample 02_SceneViewer
call :TestSample 03_Readback
call :TestSample 04_AsyncCompute
call :TestSample 05_Multithreading
call :TestSample 06_MultiGPU
call :TestSample 07_RayTracing_Triangle
call :TestSample 08_RayTracing_Boxes
call :TestSample 09_RayTracing_NRD

popd
pause

exit /b

::========================================================================================
:TestSample

echo.
echo %1

set CMD=%1.exe --api=D3D11 --frameNum=%FRAME_NUM%
echo|set /p=    D3D11  -
%CMD% && (echo  OK) || (echo  FAILED!)

set CMD=%1.exe --api=D3D12 --frameNum=%FRAME_NUM%
echo|set /p=    D3D12  -
%CMD% && (echo  OK) || (echo  FAILED!)

set CMD=%1.exe --api=VULKAN --frameNum=%FRAME_NUM%
echo|set /p=    VULKAN -
%CMD% && (echo  OK) || (echo  FAILED!)

exit /b
