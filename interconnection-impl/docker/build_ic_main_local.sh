#!/usr/bin/env bash
# 在 morse-ecdh-header 容器内编译 ic_main，使用本地依赖
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE="secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/morse-ecdh-header:20251113"
PLATFORM="linux/amd64"

SECRETFLOW_DIR="$(cd "${PROJECT_DIR}/.." && pwd)"
YACL_DIR="$(cd "${SECRETFLOW_DIR}/../yacl" && pwd)"
BAZEL_CACHE="${PROJECT_DIR}/.cache/bazelisk"
BAZEL_HOME="${PROJECT_DIR}/.cache/bazel-home"
mkdir -p "${BAZEL_CACHE}" "${BAZEL_HOME}"

echo "=== 在 ${IMAGE} (${PLATFORM}) 内编译 ic_main（使用本地依赖） ==="
echo "=== 本地 yacl 目录: ${YACL_DIR} ==="

docker run --rm --cpus=8 --memory=8g --platform "${PLATFORM}" --entrypoint bash \
  -v "${SECRETFLOW_DIR}:/secretflow" \
  -v "${YACL_DIR}:/yacl_local" \
  -v "${BAZEL_CACHE}:/opt/bazel:ro" \
  -v "${BAZEL_HOME}:/root/.cache/bazel" \
  -w /secretflow/interconnection-impl \
  "${IMAGE}" \
  -c 'set -euo pipefail
export GOPROXY="${GOPROXY:-https://goproxy.cn,direct}"
BAZEL_BIN="/opt/bazel/bazel-6.5.0-linux-x86_64"
if [[ ! -x "${BAZEL_BIN}" ]]; then
  echo "错误: 未找到预置 Bazel" >&2
  exit 1
fi
if ! python3 -c "import numpy" 2>/dev/null; then
  PIP_INDEX="${PIP_INDEX:-https://pypi.tuna.tsinghua.edu.cn/simple}"
  pip3 install -q numpy -i "${PIP_INDEX}" --trusted-host pypi.tuna.tsinghua.edu.cn \
    || pip install -q numpy -i "${PIP_INDEX}" --trusted-host pypi.tuna.tsinghua.edu.cn
fi

# 1. 修补 SPU 的 repositories.bzl（eigen/xla 等 file:// URL 替换 + sha256 移除）
echo "=== 修补 SPU repositories.bzl ==="
SPU_BZL="/root/.cache/bazel/_bazel_root/bafeb90ed63988f3d082af2d35f4f937/external/spulib/bazel/repositories.bzl"
if [[ -f "$SPU_BZL" ]]; then
  python3 /secretflow/interconnection-impl/.cache/patch_spu_deps.py "$SPU_BZL"
  sed -i "/sha256 = \"/d" "$SPU_BZL" 2>/dev/null || true
  sed -i "/sha256 = [A-Z][A-Z_]*_SHA256/d" "$SPU_BZL" 2>/dev/null || true
  echo "  SPU repositories.bzl 修补完成"
fi

# 1.5 修补 LLVM workspace.bzl 和 tsl repo.bzl（使用本地 file:// URL，避免下载 207MB LLVM 源代码）
echo "=== 修补 LLVM 依赖 ==="
python3 /secretflow/interconnection-impl/.cache/patch_llvm_deps.py
echo "  LLVM 依赖修补完成"

# 1.6 修补 yacl/psi/xla/rules_python 等 bzl 文件（重新运行全量补丁，防止 Bazel server 重启覆盖）
echo "=== 重新应用全量 bzl 补丁 ==="
python3 /secretflow/interconnection-impl/.cache/patch_all_bzl.py
echo "  全量 bzl 补丁完成"

# 2. 编译 ic_main（使用 --override_repository 绕过 GitHub 下载）
# protoc-gen-validate 和 googletest 在 grpc_deps.bzl 中只有 GitHub URL（无镜像），
# 容器内无法访问 GitHub，使用 --override_repository 直接指向本地解压目录
OVERRIDE_DIR="/secretflow/interconnection-impl/.cache/overridden"

# 清理上次失败遗留的 sandbox 目录（避免 unlinkat "Directory not empty" 错误）
SANDBOX_DIR="/root/.cache/bazel/_bazel_root/bafeb90ed63988f3d082af2d35f4f937/sandbox"
if [[ -d "$SANDBOX_DIR" ]]; then
  echo "=== 清理 stale sandbox ==="
  rm -rf "$SANDBOX_DIR" 2>/dev/null || true
fi

# 将本地修改的 blackbox_service.proto（TransportOutbound 字段编号修复）复制到 Bazel 外部缓存
# @yacl 是 git_repository，Bazel 使用缓存中的版本，不会读取本地修改
# 直接覆盖缓存中的 proto 文件，强制 Bazel 重新编译 proto 和依赖它的 C++ 代码
YACL_EXTERNAL="/root/.cache/bazel/_bazel_root/bafeb90ed63988f3d082af2d35f4f937/external/yacl"
if [[ -d "${YACL_EXTERNAL}/bazel" ]]; then
  cp /yacl_local/bazel/lib25519.BUILD "${YACL_EXTERNAL}/bazel/lib25519.BUILD"
  cp /yacl_local/yacl/crypto/ecc/lib25519/lib25519_private.h "${YACL_EXTERNAL}/yacl/crypto/ecc/lib25519/lib25519_private.h"
fi
LIB25519_EXTERNAL="/root/.cache/bazel/_bazel_root/bafeb90ed63988f3d082af2d35f4f937/external/lib25519"
if [[ -d "$LIB25519_EXTERNAL" ]]; then
  cp /yacl_local/bazel/lib25519.BUILD "$LIB25519_EXTERNAL/BUILD.bazel"
fi
PROTO_REL_PATH="yacl/link/transport/blackbox_interconnect/blackbox_service.proto"
if [[ -f "${YACL_EXTERNAL}/${PROTO_REL_PATH}" ]]; then
  echo "=== 覆盖 Bazel 缓存中的 blackbox_service.proto ==="
  cp "/yacl_local/${PROTO_REL_PATH}" "${YACL_EXTERNAL}/${PROTO_REL_PATH}"
  echo "  已复制修复后的 proto（metadata=1, payload=2, code=3, message=4）"
  # 删除 Bazel action cache 中该 proto 的编译产物，强制重新编译
  find /root/.cache/bazel/_bazel_root/bafeb90ed63988f3d082af2d35f4f937/execroot -name "blackbox_service*.pb.*" -delete 2>/dev/null || true
  find /root/.cache/bazel/_bazel_root/bafeb90ed63988f3d082af2d35f4f937/execroot -name "blackbox_service*.desc" -delete 2>/dev/null || true
else
  echo "警告: Bazel 外部缓存中未找到 yacl proto，首次构建将自动拉取" >&2
fi

echo "=== 开始编译 ic_main（使用 --override_repository） ==="
"${BAZEL_BIN}" build -c opt \
  --jobs=4 --local_cpu_resources=4 --local_ram_resources=6144 \
  --spawn_strategy=standalone \
  --override_repository=com_envoyproxy_protoc_gen_validate="${OVERRIDE_DIR}/com_envoyproxy_protoc_gen_validate" \
  --override_repository=com_google_googletest="${OVERRIDE_DIR}/com_google_googletest" \
  --override_repository=com_github_intel_ipp="${OVERRIDE_DIR}/com_github_intel_ipp" \
  ic_impl/ic_main
if command -v file >/dev/null 2>&1; then
  file bazel-bin/ic_impl/ic_main
fi
ls -la bazel-bin/ic_impl/ic_main
'

IC_MAIN_BIN=""
for candidate in "${BAZEL_HOME}"/_bazel_root/*/execroot/interconnection-impl/bazel-out/k8-opt/bin/ic_impl/ic_main; do
  if [[ -x "$candidate" ]]; then
    IC_MAIN_BIN="$candidate"
    break
  fi
done
if [[ -z "$IC_MAIN_BIN" ]] || ! file "$IC_MAIN_BIN" | grep -qE 'ELF 64-bit.*x86-64|ELF 64-bit.*AMD64'; then
  echo "错误: ic_main 不是 linux/amd64 (x86-64)" >&2
  [[ -z "$IC_MAIN_BIN" ]] || file "$IC_MAIN_BIN" >&2
  exit 1
fi
echo "=== ic_main amd64 编译完成 ==="
