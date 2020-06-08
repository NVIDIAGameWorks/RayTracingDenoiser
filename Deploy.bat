@echo off

:: =================================================================================================================================================================================
:: SETTINGS

set VK_SDK_PATH="C:\VulkanSDK\1.2.135.0"
set WIN_SDK_PATH="C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\"
set DATA_FOLDER=_Data

:: =================================================================================================================================================================================
:: CHECK FOR SDKs

if NOT exist %VK_SDK_PATH% call :ErrorOccured "Please, install Vulkan SDK v1.2.135 (or modify this file to use another location of version)"
if NOT exist %WIN_SDK_PATH% call :ErrorOccured "Please, install Windows SDK v10.0.19041.0 (or modify this file to use another location of version)"

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
call "_Build\HostDeps\premake\premake5.exe" --file="%NAME%" vs2017
set ERR=%ERRORLEVEL%
del /q "%NAME%"
if %ERR% neq 0 call :ErrorOccured

copy "ProjectBase\.editorconfig" "_Compiler\"
copy "ProjectBase\.args" "_Compiler\vs2017\00_Clear\00_Clear.args.json"
copy "ProjectBase\.args" "_Compiler\vs2017\01_Triangle\01_Triangle.args.json"
copy "ProjectBase\.args" "_Compiler\vs2017\02_SceneViewer\02_SceneViewer.args.json"
copy "ProjectBase\.args" "_Compiler\vs2017\03_Readback\03_Readback.args.json"
copy "ProjectBase\.args" "_Compiler\vs2017\04_AsyncCompute\04_AsyncCompute.args.json"
copy "ProjectBase\.args" "_Compiler\vs2017\05_Multithreading\05_Multithreading.args.json"
copy "ProjectBase\.args" "_Compiler\vs2017\06_MultiGPU\06_MultiGPU.args.json"
copy "ProjectBase\.args" "_Compiler\vs2017\07_RayTracing_Triangle\07_RayTracing_Triangle.args.json"
copy "ProjectBase\.args" "_Compiler\vs2017\08_RayTracing_Boxes\08_RayTracing_Boxes.args.json"
copy "ProjectBase\.args" "_Compiler\vs2017\09_RayTracing_NRD\09_RayTracing_NRD.args.json"

del /q "%DATA_FOLDER%\*.dll"
copy "_Build\TargetDeps\Assimp\Bin\*.dll" "%DATA_FOLDER%\"
copy "_Build\TargetDeps\AGS\lib\*.dll" "%DATA_FOLDER%\"
copy "External\NGX\*.dll" "%DATA_FOLDER%\"

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
