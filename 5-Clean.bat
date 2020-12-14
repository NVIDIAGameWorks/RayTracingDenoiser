@echo off

set /P M=Do you want to delete PACKMAN repository? [y/n]
if /I "%M%" neq "y" goto KEEP_PACKMAN

:DELETE_PACKMAN
rd /q /s "%PM_PACKAGES_ROOT%"

:KEEP_PACKMAN
rd /q /s "_Build"
rd /q /s "_Compiler"
rd /q /s "_Data"
rd /q /s "ThirdParty\DirectXTex\Shaders\Compiled"
del /q "NRD\NRD.APS"
del /q "Dependencies.xml"
del /q "Premake5.lua"
