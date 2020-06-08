@echo off

set DIR=_Build\vs2017\Bin\Release\

if not exist %DIR% (
    set DIR=_Build\vs2017\Bin\Debug\
)

if not exist %DIR% (
    echo The project is not compiled!
    pause
    exit /b
)

pushd %DIR%

09_RayTracing_NRD.exe --width=1920 --height=1080 --api=D3D12 --testMode --scene=Bistro/BistroInterior.fbx

popd

exit /b
