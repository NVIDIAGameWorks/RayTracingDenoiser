set TARGET_VS=vs2017
if exist "_Compiler\vs2019" set TARGET_VS=vs2019

rd /q /s "_NRI_SDK"
mkdir "_NRI_SDK"
cd "_NRI_SDK"
mkdir "Lib"
mkdir "Lib\Debug"
mkdir "Lib\Release"
mkdir "Include"
mkdir "Include\Extensions"
copy "..\_Build\%TARGET_VS%\Lib\Debug\NRI*.lib" "Lib\Debug"
copy "..\_Build\%TARGET_VS%\Lib\Release\NRI*.lib" "Lib\Release"
copy "..\NRI\Include\*" "Include"
copy "..\NRI\Include\Extensions\*" "Include\Extensions"
