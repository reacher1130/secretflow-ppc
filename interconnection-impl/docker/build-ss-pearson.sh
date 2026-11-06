#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${SCRIPT_DIR}/.."
rm -rf build-ss-pearson
mkdir -p build-ss-pearson/docker-conf
bazel build -c opt ic_impl/ic_main
cp bazel-bin/ic_impl/ic_main build-ss-pearson/
cp docker/run.py build-ss-pearson/
cp docker/docker-conf/entrypoint.sh build-ss-pearson/docker-conf/
cp docker/Dockerfile.bfia build-ss-pearson/Dockerfile
cd build-ss-pearson && docker build -f Dockerfile -t secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/ss-pearson:latest .
cd "${SCRIPT_DIR}/.." && rm -rf build-ss-pearson
