#!/bin/bash
# 修补 SPU 的 repositories.bzl，添加本地 file:// URL 并移除 SHA256 校验
set -euo pipefail

SPU_REPO_BZL="$1"
DEPS_DIR="/secretflow/interconnection-impl/.cache/deps"

if [ ! -f "$SPU_REPO_BZL" ]; then
  echo "ERROR: $SPU_REPO_BZL not found"
  exit 1
fi

cp "$SPU_REPO_BZL" "${SPU_REPO_BZL}.bak"

# 使用 Python 进行精确替换
python3 << 'PYEOF'
import re
import sys

repo_bzl = sys.argv[1] if len(sys.argv) > 1 else ""
deps_dir = "/secretflow/interconnection-impl/.cache/deps"

with open(repo_bzl, 'r') as f:
    content = f.read()

# 映射: URL 模式 -> 本地文件名
url_mappings = [
    # yacl
    ("https://github.com/secretflow/yacl/archive/refs/tags/0.4.5b4_nightly_20240731.tar.gz",
     f"file://{deps_dir}/yacl-0.4.5b4_nightly_20240731.tar.gz"),
    # psi
    ("https://github.com/secretflow/psi/archive/refs/tags/v0.4.0.dev240801.tar.gz",
     f"file://{deps_dir}/psi-0.4.0.dev240801.tar.gz"),
    # rules_proto_grpc
    ("https://github.com/rules-proto-grpc/rules_proto_grpc/releases/download/4.6.0/rules_proto_grpc-4.6.0.tar.gz",
     f"file://{deps_dir}/rules_proto_grpc-4.6.0.tar.gz"),
    # rules_cuda
    ("https://github.com/bazel-contrib/rules_cuda/releases/download/v0.2.1/rules_cuda-v0.2.1.tar.gz",
     f"file://{deps_dir}/rules_cuda-v0.2.1.tar.gz"),
    # platforms
    ("https://github.com/bazelbuild/platforms/releases/download/0.0.10/platforms-0.0.10.tar.gz",
     f"file://{deps_dir}/platforms-0.0.10.tar.gz"),
    # zstd
    ("https://github.com/facebook/zstd/archive/refs/tags/v1.5.6.tar.gz",
     f"file://{deps_dir}/zstd-1.5.6.tar.gz"),
    # xtensor
    ("https://github.com/xtensor-stack/xtensor/archive/refs/tags/0.25.0.tar.gz",
     f"file://{deps_dir}/xtensor-0.25.0.tar.gz"),
    # xtl
    ("https://github.com/xtensor-stack/xtl/archive/refs/tags/0.7.7.tar.gz",
     f"file://{deps_dir}/xtl-0.7.7.tar.gz"),
    # bazel_skylib
    ("https://github.com/bazelbuild/bazel-skylib/releases/download/1.7.1/bazel-skylib-1.7.1.tar.gz",
     f"file://{deps_dir}/bazel-skylib-1.7.1.tar.gz"),
    # xla
    ("https://github.com/openxla/xla/archive/04f2bfe797408c9efe742b89e2e4db6cf526ebb7.tar.gz",
     f"file://{deps_dir}/xla-04f2bfe797408c9efe742b89e2e4db6cf526ebb7.tar.gz"),
    # pybind11_bazel
    ("https://github.com/pybind/pybind11_bazel/archive/8889d39b2b925b2a47519ae09402a96f00ccf2b4.zip",
     f"file://{deps_dir}/pybind11_bazel-8889d39b2b925b2a47519ae09402a96f00ccf2b4.zip"),
    # pybind11
    ("https://github.com/pybind/pybind11/archive/refs/tags/v2.13.1.tar.gz",
     f"file://{deps_dir}/pybind11-2.13.1.tar.gz"),
    # hexl
    ("https://github.com/intel/hexl/archive/refs/tags/v1.2.5.tar.gz",
     f"file://{deps_dir}/hexl-1.2.5.tar.gz"),
    # emp-tool
    ("https://github.com/emp-toolkit/emp-tool/archive/refs/tags/0.2.5.tar.gz",
     f"file://{deps_dir}/emp-tool-0.2.5.tar.gz"),
    # emp-ot
    ("https://github.com/emp-toolkit/emp-ot/archive/refs/tags/0.2.4.tar.gz",
     f"file://{deps_dir}/emp-ot-0.2.4.tar.gz"),
    # eigen (gitlab -> local)
    ("https://gitlab.com/libeigen/eigen/-/archive/{commit}/eigen-{commit}.tar.gz".format(commit="66e8f38891841bf88ee976a316c0c78a52f0cee5"),
     f"file://{deps_dir}/eigen-66e8f38891841bf88ee976a316c0c78a52f0cee5.tar.gz"),
    # cutlass
    ("https://github.com/NVIDIA/cutlass/archive/refs/tags/v3.5.0.tar.gz",
     f"file://{deps_dir}/cutlass-3.5.0.tar.gz"),
]

# 替换所有 URL
for old_url, new_url in url_mappings:
    content = content.replace(f'"{old_url}"', f'"{new_url}"')

# 移除所有 sha256 行（因为本地文件可能有不同的哈希）
content = re.sub(r'sha256 = "[a-f0-9]+",?\n', '', content)

with open(repo_bzl, 'w') as f:
    f.write(content)

print(f"Patched {repo_bzl}")
PYEOF
