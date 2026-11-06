#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${SCRIPT_DIR}/.."
rm -rf build-ss-lr
mkdir -p build-ss-lr/docker-conf
bazel build -c opt ic_impl/ic_main
cp bazel-bin/ic_impl/ic_main build-ss-lr/
cp docker/run.py build-ss-lr/
cp docker/docker-conf/entrypoint.sh build-ss-lr/docker-conf/
cp docker/Dockerfile.bfia build-ss-lr/Dockerfile
cd build-ss-lr && docker build -f Dockerfile -t secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/ss-lr:latest .
cd "${SCRIPT_DIR}/.." && rm -rf build-ss-lr
