#!/bin/bash

cd ..
rm -rf build
mkdir build
bazel build -c opt ic_impl/ic_main
cp bazel-bin/ic_impl/ic_main build
cp -r ic_impl/data build
cp docker/Dockerfile build
cd build && docker build -f Dockerfile -t secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/ecdh-psi:latest .
