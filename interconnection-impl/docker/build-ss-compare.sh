#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${SCRIPT_DIR}/.."
rm -rf build-ss-compare
mkdir -p build-ss-compare/docker-conf
bazel build -c opt ic_impl/ic_main
cp bazel-bin/ic_impl/ic_main build-ss-compare/
cp docker/run.py build-ss-compare/
cp docker/docker-conf/entrypoint.sh build-ss-compare/docker-conf/
cp docker/Dockerfile.bfia build-ss-compare/Dockerfile
cd build-ss-compare && docker build -f Dockerfile -t secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/ss-compare:latest .
cd "${SCRIPT_DIR}/.." && rm -rf build-ss-compare
