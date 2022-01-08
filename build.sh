#!/bin/bash

mkdir build
pushd build

clang -o $1 ../$1.cc

popd

