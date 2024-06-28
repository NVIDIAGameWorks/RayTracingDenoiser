#!/bin/bash

NRD_DIR=.

rm -rf "_NRD_SDK"

mkdir -p "_NRD_SDK/Include"
mkdir -p "_NRD_SDK/Lib/Debug"
mkdir -p "_NRD_SDK/Lib/Release"
mkdir -p "_NRD_SDK/Shaders"
mkdir -p "_NRD_SDK/Shaders/Include"

cd "_NRD_SDK"

cp -r ../$NRD_DIR/Include/ "Include"
cp -H ../_Bin/Debug/libNRD.so "Lib/Debug"
cp -H ../_Bin/Release/libNRD.so "Lib/Release"
cp ../$NRD_DIR/Shaders/Include/NRD.hlsli "Shaders/Include"
cp ../$NRD_DIR/Shaders/Include/NRDEncoding.hlsli "Shaders/Include"
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

read -p "Do you need NRD integration layer? [y/n]" -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]
then
    mkdir -p "Integration"

    cp -r ../$NRD_DIR/Integration/ "Integration"
fi

cd ..
