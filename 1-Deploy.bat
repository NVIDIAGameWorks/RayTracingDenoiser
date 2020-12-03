@echo off

:: =================================================================================================================================================================================
:: SETTINGS

set WIN_SDK_PATH=C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\
set DATA_FOLDER=_Data
set TARGET_VS=%1
if "%1"=="" set TARGET_VS="vs2017"

:: =================================================================================================================================================================================
:: CHECK FOR SDKs

if "%VULKAN_SDK%" == "" call :ErrorOccured "Please, install Vulkan SDK"
if NOT exist "%WIN_SDK_PATH%" call :ErrorOccured "Please, install Windows SDK v10.0.19041.0 (or modify this file to use another location of version)"

:: =================================================================================================================================================================================
:: DOWNLOAD DEPENDENCIES

set NAME=Dependencies.xml
copy "ProjectBase\%NAME%" ".\"
call "ProjectBase\Packman\packman" pull "%NAME%" -p windows-x86_64
set ERR=%ERRORLEVEL%
del /q "%NAME%"
if %ERR% neq 0 call :ErrorOccured

:: =================================================================================================================================================================================
:: GENERATE PROJECTS

set NAME=Premake.lua
copy "ProjectBase\%NAME%" ".\"
call "_Build\HostDeps\premake\premake5.exe" --file="%NAME%" %TARGET_VS%
set ERR=%ERRORLEVEL%
del /q "%NAME%"
if %ERR% neq 0 call :ErrorOccured

copy "ProjectBase\.editorconfig" "_Compiler\"
copy "ProjectBase\.args" "_Compiler\%TARGET_VS%\00_Clear\00_Clear.args.json"
copy "ProjectBase\.args" "_Compiler\%TARGET_VS%\01_Triangle\01_Triangle.args.json"
copy "ProjectBase\.args" "_Compiler\%TARGET_VS%\02_SceneViewer\02_SceneViewer.args.json"
copy "ProjectBase\.args" "_Compiler\%TARGET_VS%\03_Readback\03_Readback.args.json"
copy "ProjectBase\.args" "_Compiler\%TARGET_VS%\04_AsyncCompute\04_AsyncCompute.args.json"
copy "ProjectBase\.args" "_Compiler\%TARGET_VS%\05_Multithreading\05_Multithreading.args.json"
copy "ProjectBase\.args" "_Compiler\%TARGET_VS%\06_MultiGPU\06_MultiGPU.args.json"
copy "ProjectBase\.args" "_Compiler\%TARGET_VS%\07_RayTracing_Triangle\07_RayTracing_Triangle.args.json"
copy "ProjectBase\.args" "_Compiler\%TARGET_VS%\08_RayTracing_Boxes\08_RayTracing_Boxes.args.json"
copy "ProjectBase\.args" "_Compiler\%TARGET_VS%\09_RayTracing_NRD\09_RayTracing_NRD.args.json"

del /q "%DATA_FOLDER%\*.dll"
copy "_Build\TargetDeps\Assimp\Bin\*.dll" "%DATA_FOLDER%\"
copy "_Build\TargetDeps\AGS\lib\*.dll" "%DATA_FOLDER%\"

copy "Tests\Bistro.bin" "%DATA_FOLDER%\Scenes\Bistro\tests.bin"

:: =================================================================================================================================================================================
:: ALL GOOD

exit /b 0

:: =================================================================================================================================================================================
:: ERROR

:ErrorOccured

echo.
echo %~1
echo.

pause

exit /b 1
