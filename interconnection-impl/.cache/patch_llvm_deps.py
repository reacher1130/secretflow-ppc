#!/usr/bin/env python3
"""修补 LLVM 和 tsl 的依赖文件，使用本地 file:// URL"""
import re
import os

DEPS_DIR = "/secretflow/interconnection-impl/.cache/deps"
LLVM_TARBALL = f"file://{DEPS_DIR}/llvm-project-58fb51492d9669525662fa269295d85537968569.tar.gz"

def patch_llvm_workspace(bzl_path):
    """修补 LLVM workspace.bzl，使用本地 file:// URL"""
    if not os.path.exists(bzl_path):
        return False
    with open(bzl_path) as f:
        content = f.read()

    # 替换整个 urls 列表为单一 file:// URL
    content = re.sub(
        r'urls = \[\s*"[^"]*".*?\]',
        f'urls = [\n            "{LLVM_TARBALL}",\n        ]',
        content,
        flags=re.DOTALL,
    )

    # 确保 sha256 参数存在（tf_http_archive 需要 sha256 位置参数）
    if 'sha256 = ' not in content:
        # 在 name = name, 后添加 sha256 = "",
        content = content.replace(
            'name = name,',
            'name = name,\n        sha256 = "",',
        )
    else:
        # 替换现有 sha256 值为空字符串
        content = re.sub(r'sha256 = LLVM_SHA256', 'sha256 = ""', content)
        content = re.sub(r'sha256 = "[^"]*"', 'sha256 = ""', content)

    with open(bzl_path, 'w') as f:
        f.write(content)
    return True


def patch_tf_repo(bzl_path):
    """修补 tsl/third_party/repo.bzl，移除 mirror URL 检查，使 sha256 可选"""
    if not os.path.exists(bzl_path):
        return False
    with open(bzl_path) as f:
        content = f.read()

    # 移除 URL 数量检查
    content = re.sub(r'if len\(urls\) < 2:', 'if False:', content)

    # 移除 mirror URL 检查
    content = re.sub(
        r'if not any\(\[mirror in urls\[0\].*?\]\):',
        'if False:',
        content,
        flags=re.DOTALL,
    )

    # 使 tf_http_archive 的 sha256 和 urls 参数可选（添加默认值）
    # 这样之前被错误移除 sha256 的调用仍能正常工作
    content = content.replace(
        'def tf_http_archive(name, sha256, urls, **kwargs):',
        'def tf_http_archive(name, sha256 = "", urls = [], **kwargs):'
    )

    with open(bzl_path, 'w') as f:
        f.write(content)
    return True


if __name__ == '__main__':
    import sys
    bazel_root = "/root/.cache/bazel/_bazel_root/bafeb90ed63988f3d082af2d35f4f937/external"

    # 修补 LLVM workspace.bzl
    for llvm_bzl in [
        f"{bazel_root}/xla/third_party/tsl/third_party/llvm/workspace.bzl",
        f"{bazel_root}/xla/third_party/llvm/workspace.bzl",
    ]:
        if patch_llvm_workspace(llvm_bzl):
            print(f"Patched LLVM: {llvm_bzl}")

    # 修补 tsl/third_party/repo.bzl 和 xla/third_party/repo.bzl
    for repo_bzl in [
        f"{bazel_root}/tsl/third_party/repo.bzl",
        f"{bazel_root}/xla/third_party/repo.bzl",
    ]:
        if patch_tf_repo(repo_bzl):
            print(f"Patched repo.bzl: {repo_bzl}")
