#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${SCRIPT_DIR}/.."
rm -rf build-ss-vif
mkdir -p build-ss-vif/docker-conf
bazel build -c opt ic_impl/ic_main
cp bazel-bin/ic_impl/ic_main build-ss-vif/
cp docker/run.py build-ss-vif/
cp docker/docker-conf/entrypoint.sh build-ss-vif/docker-conf/
cp docker/Dockerfile.bfia build-ss-vif/Dockerfile
cd build-ss-vif && docker build -f Dockerfile -t secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/ss-vif:latest .
cd "${SCRIPT_DIR}/.." && rm -rf build-ss-vif
