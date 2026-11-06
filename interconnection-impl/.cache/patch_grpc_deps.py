#!/usr/bin/env python3
"""修补 grpc_deps.bzl，为 GitHub 依赖添加本地 file:// URL 镜像"""
import re
import os

DEPS_DIR = "/secretflow/interconnection-impl/.cache/deps"

# grpc_deps.bzl 中只有单一 github.com URL（无 mirror）的依赖
# 这些是最容易超时失败的
SINGLE_URL_DEPS = {
    "com_envoyproxy_protoc_gen_validate": (
        "protoc-gen-validate-4694024279bdac52b77e22dc87808bd0fd732b69",
        "https://github.com/envoyproxy/protoc-gen-validate/archive/4694024279bdac52b77e22dc87808bd0fd732b69.tar.gz",
        f"file://{DEPS_DIR}/protoc-gen-validate-4694024279bdac52b77e22dc87808bd0fd732b69.tar.gz",
    ),
    "com_google_googletest": (
        "googletest-0e402173c97aea7a00749e825b194bfede4f2e45",
        "https://github.com/google/googletest/archive/0e402173c97aea7a00749e825b194bfede4f2e45.tar.gz",
        f"file://{DEPS_DIR}/googletest-0e402173c97aea7a00749e825b194bfede4f2e45.tar.gz",
    ),
}


def patch_grpc_deps(bzl_path):
    """修补 grpc_deps.bzl，为 GitHub URL 添加本地 file:// 镜像"""
    if not os.path.exists(bzl_path):
        print(f"  跳过（文件不存在）: {bzl_path}")
        return False

    with open(bzl_path) as f:
        content = f.read()

    original = content

    for dep_name, (prefix, github_url, file_url) in SINGLE_URL_DEPS.items():
        local_file = file_url.replace(f"file://{DEPS_DIR}/", "")
        # 只有当本地文件存在时才替换
        host_path = f"/Users/gaofeng/develop/project/Privacy-Preserving-Computing-Interoperability/NewProject/secretflow/interconnection-impl/.cache/deps/{os.path.basename(file_url)}"
        if not os.path.exists(host_path):
            print(f"  跳过 {dep_name}（本地文件不存在: {os.path.basename(file_url)}）")
            continue

        # 将单一 github URL 替换为 [file://, github://] 双 URL
        old_pattern = f'            urls = [\n                "{github_url}",\n            ],'
        new_pattern = f'            urls = [\n                "{file_url}",\n                "{github_url}",\n            ],'
        if old_pattern in content:
            content = content.replace(old_pattern, new_pattern)
            print(f"  已为 {dep_name} 添加 file:// 镜像")
        else:
            # 尝试其他可能的格式
            old_pattern2 = f'                "{github_url}",\n            ],'
            new_pattern2 = f'                "{file_url}",\n                "{github_url}",\n            ],'
            if old_pattern2 in content:
                content = content.replace(old_pattern2, new_pattern2)
                print(f"  已为 {dep_name} 添加 file:// 镜像（格式2）")
            else:
                # 直接替换 URL 字符串，在其前面添加 file:// URL
                if github_url in content:
                    content = content.replace(
                        f'"{github_url}"',
                        f'"{file_url}",\n                "{github_url}"',
                    )
                    print(f"  已为 {dep_name} 添加 file:// 镜像（格式3）")
                else:
                    print(f"  警告: 未找到 {dep_name} 的 URL 模式")

    if content != original:
        with open(bzl_path, 'w') as f:
            f.write(content)
        return True
    return False


if __name__ == '__main__':
    bazel_root = "/root/.cache/bazel/_bazel_root/bafeb90ed63988f3d082af2d35f4f937/external"
    grpc_bzl = f"{bazel_root}/com_github_grpc_grpc/bazel/grpc_deps.bzl"
    if patch_grpc_deps(grpc_bzl):
        print(f"Patched {grpc_bzl}")
    else:
        print(f"No changes needed for {grpc_bzl}")
