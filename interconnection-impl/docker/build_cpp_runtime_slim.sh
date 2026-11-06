#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PLATFORM="${BFIA_CPP_BUILD_PLATFORM:-linux/amd64}"
IMAGE="${BFIA_CPP_RUNTIME_IMAGE:-secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/bfia-cpp-runtime:linux-amd64-slim}"

find_ic_main() {
  local candidate
  for candidate in \
    "$PROJECT_DIR"/.cache/bazel-home/_bazel_root/*/execroot/interconnection-impl/bazel-out/k8-opt/bin/ic_impl/ic_main \
    "$PROJECT_DIR"/bazel-bin/ic_impl/ic_main
  do
    if [[ -x "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

IC_MAIN_BIN="$(find_ic_main || true)"
if [[ -z "$IC_MAIN_BIN" ]]; then
  echo "未找到 ic_main，执行 Bazel 构建..."
  (cd "$PROJECT_DIR" && bazel build -c opt ic_impl/ic_main)
  IC_MAIN_BIN="$(find_ic_main || true)"
fi

if [[ -z "$IC_MAIN_BIN" ]]; then
  echo "错误：构建后仍未找到可执行的 ic_main" >&2
  exit 1
fi

if ! file "$IC_MAIN_BIN" | grep -qE 'ELF 64-bit.*x86-64|ELF 64-bit.*AMD64'; then
  echo "错误：ic_main 不是 Linux amd64 可执行文件" >&2
  file "$IC_MAIN_BIN" >&2
  exit 1
fi

BUILD_CONTEXT="$(mktemp -d "${TMPDIR:-/tmp}/bfia-cpp-runtime.XXXXXX")"
trap 'rm -rf -- "$BUILD_CONTEXT"' EXIT
cp "$IC_MAIN_BIN" "$BUILD_CONTEXT/ic_main"

echo "=== 构建 BFIA C++ 精简运行镜像 ==="
echo "平台: $PLATFORM"
echo "镜像: $IMAGE"
echo "ic_main: $IC_MAIN_BIN"

docker build \
  --platform "$PLATFORM" \
  -f "$SCRIPT_DIR/Dockerfile.cpp-runtime" \
  -t "$IMAGE" \
  "$BUILD_CONTEXT"

docker image inspect "$IMAGE" \
  --format '构建完成：{{.RepoTags}} size={{.Size}} bytes platform={{.Architecture}}/{{.Os}}'
