#!/usr/bin/env python3
"""在主机上运行：扫描所有 bzl 文件，下载 GitHub 依赖，修补 bzl 文件使用 file:// URL"""
import re
import os
import sys
import subprocess

# 主机路径
HOST_DEPS_DIR = "/Users/gaofeng/develop/project/Privacy-Preserving-Computing-Interoperability/NewProject/secretflow/interconnection-impl/.cache/deps"
HOST_BAZEL_EXTERNAL = "/Users/gaofeng/develop/project/Privacy-Preserving-Computing-Interoperability/NewProject/secretflow/interconnection-impl/.cache/bazel-home/_bazel_root/bafeb90ed63988f3d082af2d35f4f937/external"
# 容器内路径（用于 file:// URL）
CONTAINER_DEPS_DIR = "/secretflow/interconnection-impl/.cache/deps"
GHFAST = "https://ghfast.top/"

def find_bzl_files():
    """找到所有 .bzl 和 WORKSPACE 文件"""
    bzl_files = []
    for root, dirs, files in os.walk(HOST_BAZEL_EXTERNAL):
        # 跳过隐藏目录
        dirs[:] = [d for d in dirs if not d.startswith('.') and d != '__pycache__']
        for f in files:
            if f.endswith('.bzl') or f == 'WORKSPACE':
                bzl_files.append(os.path.join(root, f))
    return bzl_files

def extract_urls(content):
    """提取所有 GitHub/GitLab 下载 URL"""
    # 匹配引号内的 URL
    urls = set(re.findall(r'"(https://github\.com/[^"]+|https://gitlab\.com/[^"]+)"', content))
    # 过滤只保留下载 URL
    download_urls = set()
    for url in urls:
        if any(p in url for p in ['/archive/', '/releases/download/', '.tar.gz', '.zip']):
            download_urls.add(url)
    return download_urls

def url_to_filename(url):
    """从 URL 生成文件名"""
    url_clean = url.split('?')[0]
    name = os.path.basename(url_clean)
    # 清理特殊字符
    name = re.sub(r'[^a-zA-Z0-9._-]', '_', name)
    return name

def download_url(url, filepath):
    """下载 URL 到 filepath"""
    if os.path.exists(filepath) and os.path.getsize(filepath) > 100:
        return True

    # 尝试 ghfast.top
    mirror = f"{GHFAST}{url}"
    for attempt_url in [mirror, url]:
        try:
            print(f"  下载: {os.path.basename(filepath)} ...", end="", flush=True)
            result = subprocess.run(
                ["curl", "-L", "-s", "-o", filepath, "--connect-timeout", "30", "--max-time", "300", attempt_url],
                timeout=320
            )
            if result.returncode == 0 and os.path.exists(filepath) and os.path.getsize(filepath) > 100:
                print(f" OK ({os.path.getsize(filepath)} bytes)")
                return True
            else:
                print(f" FAILED")
        except subprocess.TimeoutExpired:
            print(f" TIMEOUT")
        except Exception as e:
            print(f" ERROR: {e}")
    return False

def main():
    os.makedirs(HOST_DEPS_DIR, exist_ok=True)

    print("=== 扫描所有 bzl 文件 ===")
    bzl_files = find_bzl_files()
    print(f"  找到 {len(bzl_files)} 个 bzl/WORKSPACE 文件")

    # 收集所有 URL 和对应的文件内容
    all_urls = {}  # url -> filename
    file_contents = {}

    for bzl_path in bzl_files:
        try:
            with open(bzl_path) as f:
                content = f.read()
            file_contents[bzl_path] = content
            urls = extract_urls(content)
            for url in urls:
                if url not in all_urls:
                    all_urls[url] = url_to_filename(url)
        except:
            pass

    print(f"  找到 {len(all_urls)} 个唯一的下载 URL")

    # 下载所有 URL
    print("\n=== 下载所有依赖 ===")
    url_map = {}  # github_url -> container_file_url
    failed = []

    for url in sorted(all_urls.keys()):
        filename = all_urls[url]
        host_path = os.path.join(HOST_DEPS_DIR, filename)
        if download_url(url, host_path):
            container_url = f"file://{CONTAINER_DEPS_DIR}/{filename}"
            url_map[url] = container_url
        else:
            failed.append(url)

    print(f"\n  成功: {len(url_map)}, 失败: {len(failed)}")
    for u in failed:
        print(f"    FAILED: {u}")

    # 修补所有 bzl 文件
    print("\n=== 修补 bzl 文件 ===")
    total = 0
    for bzl_path, content in file_contents.items():
        original = content
        for github_url, file_url in url_map.items():
            if github_url in content:
                content = content.replace(f'"{github_url}"', f'"{file_url}"')

        # 处理 .format(commit = VAR) 模式
        format_matches = re.findall(r'"(https://github\.com/[^"]*\{commit\}[^"]*)"\.format\(commit = (\w+)\)', content)
        for url_template, var_name in format_matches:
            var_match = re.search(rf'{var_name}\s*=\s*"([a-f0-9]+)"', content)
            if var_match:
                commit = var_match.group(1)
                full_url = url_template.replace('{commit}', commit)
                filename = url_to_filename(full_url)
                host_path = os.path.join(HOST_DEPS_DIR, filename)
                if os.path.exists(host_path):
                    file_url = f"file://{CONTAINER_DEPS_DIR}/{filename}"
                    old = f'"{url_template}".format(commit = {var_name})'
                    new = f'"{file_url}"'
                    if old in content:
                        content = content.replace(old, new)
                    if full_url in content:
                        content = content.replace(f'"{full_url}"', f'"{file_url}"')

        if content != original:
            with open(bzl_path, 'w') as f:
                f.write(content)
            # 统计替换数
            count = sum(1 for u in url_map if u in original)
            print(f"  已修补: {os.path.relpath(bzl_path, HOST_BAZEL_EXTERNAL)}")
            total += 1

    print(f"\n  总共修补了 {total} 个文件")
    print("\n=== 完成 ===")

if __name__ == '__main__':
    main()
