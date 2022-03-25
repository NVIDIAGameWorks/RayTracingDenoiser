#!/bin/bash

NRD_DIR=.

rm -rf "_NRD_SDK"

mkdir -p "_NRD_SDK/Include"
mkdir -p "_NRD_SDK/Integration"
mkdir -p "_NRD_SDK/Lib/Debug"
mkdir -p "_NRD_SDK/Lib/Release"

cd "_NRD_SDK"

cp -r ../$NRD_DIR/Integration/ "Integration"
cp -r ../$NRD_DIR/Include/ "Include"
cp -H ../_Build/Debug/libNRD.so "Lib/Debug"
cp -H ../_Build/Release/libNRD.so "Lib/Release"
cp ../$NRD_DIR/LICENSE.txt "."
cp ../$NRD_DIR/README.md "."

read -p "Do you need the shader source code for a white-box integration? [y/n]" -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]
then
    mkdir -p "Shaders"

    cp -r ../$NRD_DIR/Shaders/ "Shaders"
    cp ../$NRD_DIR/External/MathLib/*.hlsli "Shaders\Source"
fi

cd ..
