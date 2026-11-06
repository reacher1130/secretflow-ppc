#!/usr/bin/env bash
# 在 linux/amd64 容器（secretflow/spu-ci）内编译 ic_main，与官方 morse-ecdh-header 同架构。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE="${SPU_CI_IMAGE:-secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/morse-ecdh-header:20251113}"
PLATFORM="${BUILD_PLATFORM:-linux/amd64}"

echo "=== 在 ${IMAGE} (${PLATFORM}) 内 bazel 编译 ic_main ==="
# 挂载 secretflow 父目录，使 WORKSPACE 中 ../interconnection 相对路径在容器内有效
SECRETFLOW_DIR="$(cd "${PROJECT_DIR}/.." && pwd)"
BAZEL_CACHE="${PROJECT_DIR}/.cache/bazelisk"
BAZEL_HOME="${PROJECT_DIR}/.cache/bazel-home"
mkdir -p "${BAZEL_CACHE}" "${BAZEL_HOME}"
docker run --rm --privileged --platform "${PLATFORM}" --entrypoint bash \
  -v "${SECRETFLOW_DIR}:/secretflow" \
  -v "${BAZEL_CACHE}:/opt/bazel:ro" \
  -v "${BAZEL_HOME}:/root/.cache/bazel" \
  -w /secretflow/interconnection-impl \
  "${IMAGE}" \
  -c 'set -euo pipefail
export GOPROXY="${GOPROXY:-https://goproxy.cn,direct}"
BAZEL_BIN="/opt/bazel/bazel-6.5.0-linux-x86_64"
if [[ ! -x "${BAZEL_BIN}" ]]; then
  echo "错误: 未找到预置 Bazel，请在本机执行 interconnection-impl/docker/build_ic_main_amd64.sh 前先下载 .cache/bazelisk/bazel-6.5.0-linux-x86_64" >&2
  exit 1
fi
if ! python3 -c "import numpy" 2>/dev/null; then
  PIP_INDEX="${PIP_INDEX:-https://pypi.tuna.tsinghua.edu.cn/simple}"
  pip3 install -q numpy -i "${PIP_INDEX}" --trusted-host pypi.tuna.tsinghua.edu.cn \
    || pip install -q numpy -i "${PIP_INDEX}" --trusted-host pypi.tuna.tsinghua.edu.cn
fi
"${BAZEL_BIN}" build -c opt ic_impl/ic_main
if command -v file >/dev/null 2>&1; then
  file bazel-bin/ic_impl/ic_main
fi
ls -la bazel-bin/ic_impl/ic_main
'

if ! file "${PROJECT_DIR}/bazel-bin/ic_impl/ic_main" | grep -qE 'x86-64|Intel 80386'; then
  echo "错误: ic_main 不是 linux/amd64 (x86-64)" >&2
  file "${PROJECT_DIR}/bazel-bin/ic_impl/ic_main" >&2
  exit 1
fi
echo "=== ic_main amd64 编译完成 ==="
