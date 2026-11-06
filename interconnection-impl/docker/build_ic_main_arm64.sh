#!/usr/bin/env bash
# 在 linux/arm64 容器（secretflow/spu-ci）内编译 ic_main，供 Mac M 系 + kuscia aarch64 使用。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE="${SPU_CI_IMAGE:-secretflow/spu-ci}"

echo "=== 在 ${IMAGE} (linux/arm64) 内 bazel 编译 ic_main ==="
docker run --rm --platform linux/arm64 --entrypoint bash \
  -v "${PROJECT_DIR}:/work" -w /work \
  "${IMAGE}" \
  -c 'set -euo pipefail
bazel build -c opt ic_impl/ic_main
file bazel-bin/ic_impl/ic_main
'

arch=$(file "${PROJECT_DIR}/bazel-bin/ic_impl/ic_main" | grep -o 'ARM aarch64\|x86-64' || true)
if [[ "${arch}" != "ARM aarch64" ]]; then
  echo "错误: ic_main 不是 ARM aarch64，当前: ${arch:-unknown}" >&2
  exit 1
fi
echo "=== ic_main arm64 编译完成 ==="
