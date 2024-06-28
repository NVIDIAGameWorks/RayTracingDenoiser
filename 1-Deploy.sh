#!/bin/bash

git submodule update --init --recursive

mkdir -p "_Build"

cd "_Build"
cmake ..
cd ..
