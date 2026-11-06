#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${SCRIPT_DIR}/.."
rm -rf build-ss-xgb
mkdir -p build-ss-xgb/docker-conf
bazel build -c opt ic_impl/ic_main
cp bazel-bin/ic_impl/ic_main build-ss-xgb/
cp docker/run.py build-ss-xgb/
cp docker/docker-conf/entrypoint.sh build-ss-xgb/docker-conf/
cp docker/Dockerfile.bfia build-ss-xgb/Dockerfile
cd build-ss-xgb && docker build -f Dockerfile -t secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/ss-xgb:latest .
cd "${SCRIPT_DIR}/.." && rm -rf build-ss-xgb
