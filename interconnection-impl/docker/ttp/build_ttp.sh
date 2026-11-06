#!/bin/bash

cd ../..
rm -rf ttp-build
mkdir ttp-build
# bazel build -c opt ic_impl/ic_main
cp beaver_impl/ttp_server/beaver_server_main ttp-build
# cp -r ic_impl/data build
cp docker/ttp/Dockerfile ttp-build
cd ttp-build && docker build -f Dockerfile -t secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/ttp-server:latest .
