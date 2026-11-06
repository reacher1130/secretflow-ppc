#!/usr/bin/env python3
"""全面下载和修补所有 GitHub 依赖，使用 ghfast.top 镜像和本地 file:// URL"""
import re
import os
import sys
import subprocess
import hashlib

DEPS_DIR = "/secretflow/interconnection-impl/.cache/deps"
BAZEL_EXTERNAL = "/root/.cache/bazel/_bazel_root/bafeb90ed63988f3d082af2d35f4f937/external"
GHFAST = "https://ghfast.top/"

def find_bzl_files():
    """找到所有 .bzl 文件"""
    bzl_files = []
    for root, dirs, files in os.walk(BAZEL_EXTERNAL):
        # 跳过某些目录
        skip = False
        for part in root.split('/'):
            if part.startswith('.') or part in ('__pycache__',):
                skip = True
                break
        if skip:
            continue
        for f in files:
            if f.endswith('.bzl') or f == 'WORKSPACE' or f.endswith('.BUILD'):
                bzl_files.append(os.path.join(root, f))
    return bzl_files

def extract_github_urls(bzl_path):
    """从 bzl 文件中提取所有 github.com 和 gitlab.com 的 URL"""
    with open(bzl_path) as f:
        content = f.read()
    # 匹配引号内的 GitHub/GitLab URL
    urls = set(re.findall(r'"(https://github\.com/[^"]+|https://gitlab\.com/[^"]+)"', content))
    # 也匹配 .format() 模式中的 URL
    format_urls = re.findall(r'"(https://github\.com/[^"]*\{commit\}[^"]*)"', content)
    return urls, content

def url_to_filename(url):
    """从 URL 提取文件名"""
    # 去除查询参数
    url_clean = url.split('?')[0]
    return os.path.basename(url_clean)

def download_url(url, filename):
    """通过 ghfast.top 镜像下载 URL"""
    filepath = os.path.join(DEPS_DIR, filename)
    if os.path.exists(filepath) and os.path.getsize(filepath) > 0:
        print(f"  已存在: {filename}")
        return True

    # 尝试 ghfast.top 镜像
    mirror_url = f"{GHFAST}{url}"
    print(f"  下载: {filename} (via ghfast.top)")
    try:
        result = subprocess.run(
            ["curl", "-L", "-o", filepath, "--connect-timeout", "30", "--max-time", "300", mirror_url],
            capture_output=True, text=True, timeout=320
        )
        if result.returncode == 0 and os.path.exists(filepath) and os.path.getsize(filepath) > 0:
            print(f"    成功: {os.path.getsize(filepath)} bytes")
            return True
    except Exception as e:
        print(f"    ghfast.top 失败: {e}")

    # 尝试直接 GitHub
    print(f"  下载: {filename} (direct)")
    try:
        result = subprocess.run(
            ["curl", "-L", "-o", filepath, "--connect-timeout", "30", "--max-time", "300", url],
            capture_output=True, text=True, timeout=320
        )
        if result.returncode == 0 and os.path.exists(filepath) and os.path.getsize(filepath) > 0:
            print(f"    成功: {os.path.getsize(filepath)} bytes")
            return True
    except Exception as e:
        print(f"    直接下载失败: {e}")

    return False

def patch_bzl_file(bzl_path, url_map):
    """修补 bzl 文件，将 GitHub URL 替换为 file:// URL"""
    with open(bzl_path) as f:
        content = f.read()

    original = content
    patched_count = 0

    for github_url, file_url in url_map.items():
        if github_url in content:
            content = content.replace(f'"{github_url}"', f'"{file_url}"')
            patched_count += 1

    if content != original:
        with open(bzl_path, 'w') as f:
            f.write(content)
        return patched_count
    return 0

def main():
    os.makedirs(DEPS_DIR, exist_ok=True)

    print("=== 扫描所有 bzl 文件中的 GitHub URL ===")
    bzl_files = find_bzl_files()
    print(f"  找到 {len(bzl_files)} 个 bzl 文件")

    all_urls = {}  # url -> set of bzl files
    bzl_contents = {}

    for bzl_path in bzl_files:
        try:
            urls, content = extract_github_urls(bzl_path)
            bzl_contents[bzl_path] = content
            for url in urls:
                if url not in all_urls:
                    all_urls[url] = set()
                all_urls[url].add(bzl_path)
        except Exception as e:
            pass

    print(f"  找到 {len(all_urls)} 个唯一的 GitHub/GitLab URL")

    # 过滤掉非下载 URL（如 issues, pulls 等）
    download_urls = {}
    for url in all_urls:
        # 只处理 archive, releases, tar.gz, zip 等下载 URL
        if any(pattern in url for pattern in ['/archive/', '/releases/download/', '.tar.gz', '.zip']):
            download_urls[url] = all_urls[url]

    print(f"  其中 {len(download_urls)} 个是下载 URL")

    # 下载所有 URL
    print("\n=== 下载所有 GitHub 依赖 ===")
    url_map = {}  # github_url -> file_url
    failed_downloads = []

    for url in sorted(download_urls.keys()):
        filename = url_to_filename(url)
        if not filename:
            continue
        # 清理文件名
        filename = re.sub(r'[^a-zA-Z0-9._-]', '_', filename)
        if download_url(url, filename):
            file_url = f"file://{DEPS_DIR}/{filename}"
            url_map[url] = file_url
        else:
            failed_downloads.append(url)

    print(f"\n  成功下载: {len(url_map)} 个")
    print(f"  失败: {len(failed_downloads)} 个")
    for u in failed_downloads:
        print(f"    FAILED: {u}")

    # 修补所有 bzl 文件
    print("\n=== 修补所有 bzl 文件 ===")
    total_patched = 0
    for bzl_path in bzl_files:
        # 只修补有 URL 需要替换的文件
        relevant_urls = {u: f for u, f in url_map.items() if u in bzl_contents.get(bzl_path, '')}
        if relevant_urls:
            count = patch_bzl_file(bzl_path, relevant_urls)
            if count > 0:
                print(f"  {bzl_path}: 替换了 {count} 个 URL")
                total_patched += count

    print(f"\n  总共替换了 {total_patched} 个 URL")

    # 处理 .format() 模式的 URL
    print("\n=== 处理 .format() 模式的 URL ===")
    for bzl_path in bzl_files:
        try:
            with open(bzl_path) as f:
                content = f.read()
        except:
            continue

        # 检查是否有 .format(commit = ...) 模式
        format_patterns = re.findall(r'"(https://github\.com/[^"]*\{commit\}[^"]*)"\.format\(commit = (\w+)\)', content)
        for url_template, var_name in format_patterns:
            # 找到变量的值
            var_match = re.search(rf'{var_name}\s*=\s*"([a-f0-9]+)"', content)
            if var_match:
                commit = var_match.group(1)
                full_url = url_template.replace('{commit}', commit)
                filename = url_to_filename(full_url)
                filename = re.sub(r'[^a-zA-Z0-9._-]', '_', filename)
                filepath = os.path.join(DEPS_DIR, filename)
                if os.path.exists(filepath):
                    file_url = f"file://{DEPS_DIR}/{filename}"
                    # 替换整个 .format() 调用
                    old = f'"{url_template}".format(commit = {var_name})'
                    new = f'"{file_url}"'
                    if old in content:
                        content = content.replace(old, new)
                        print(f"  替换 .format() URL in {bzl_path}: {filename}")

                    # 也替换已展开的 URL
                    if full_url in content:
                        content = content.replace(f'"{full_url}"', f'"{file_url}"')

            with open(bzl_path, 'w') as f:
                f.write(content)
        except:
            pass

    print("\n=== 完成 ===")

if __name__ == '__main__':
    main()
