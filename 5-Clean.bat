@echo off

:choice
set /P c=Do you want to delete PACKMAN repository? [y/n]
if /I "%c%" EQU "y" goto :deletepackman
if /I "%c%" EQU "n" goto :keeppackman
goto :choice

:deletepackman
rd /q /s "%PM_PACKAGES_ROOT%"

:keeppackman
rd /q /s "_Build"
rd /q /s "_Compiler"
rd /q /s "_Data"
rd /q /s "ThirdParty\DirectXTex\Shaders\Compiled"
del /q "NRD\NRD.APS"
del /q "Dependencies.xml"
del /q "Premake5.lua"
