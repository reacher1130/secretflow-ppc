#!/usr/bin/env python3
"""在容器内运行：重新修补所有 bzl 文件，使用 file:// URL 替换 GitHub URL。
防止 Bazel server 重启时重新拉取外部仓库覆盖已有补丁。
正确处理 %s 和 .format(commit=...) 格式字符串。"""
import re
import os
import sys

# 容器内路径
BAZEL_EXTERNAL = "/root/.cache/bazel/_bazel_root/bafeb90ed63988f3d082af2d35f4f937/external"
DEPS_DIR = "/secretflow/interconnection-impl/.cache/deps"


def find_bzl_files():
    """找到所有 .bzl 和 WORKSPACE 文件"""
    bzl_files = []
    for root, dirs, files in os.walk(BAZEL_EXTERNAL):
        dirs[:] = [d for d in dirs if not d.startswith('.') and d != '__pycache__']
        for f in files:
            if f.endswith('.bzl') or f == 'WORKSPACE':
                bzl_files.append(os.path.join(root, f))
    return bzl_files


def url_to_filename(url):
    """从 URL 生成文件名（与 download_all_github_deps.py 一致）"""
    url_clean = url.split('?')[0]
    name = os.path.basename(url_clean)
    name = re.sub(r'[^a-zA-Z0-9._-]', '_', name)
    return name


def fix_percent_s_patterns(content):
    """修复 %s 格式字符串被错误替换的问题。
    例如: "file://...deps/_s.tar.gz" % RULES_BOOST_COMMIT
    应该变成: "file://...deps/cfa585b1b5843993b70aa52707266dc23b3282d0.tar.gz"
    """
    # 匹配 "file://...deps/<filename>_s.<ext>" % VAR 模式
    # 其中 _s 是 %s 被替换后留下的痕迹
    pattern = r'"(file://[^"]*deps/[^"]*_s\.(tar\.gz|zip))"\s*%\s*(\w+)'
    matches = re.findall(pattern, content)
    for broken_url, ext, var_name in matches:
        # 查找变量定义
        var_match = re.search(rf'{var_name}\s*=\s*"([^"]+)"', content)
        if var_match:
            var_value = var_match.group(1)
            # 构造正确的 URL：将 _s 替换为变量值
            correct_url = broken_url.replace('_s.', f'{var_value}.')
            # 验证文件是否存在
            filename = os.path.basename(correct_url.replace('file://', ''))
            filepath = os.path.join(DEPS_DIR, filename)
            if os.path.exists(filepath):
                # 替换整个 "url" % VAR 表达式
                old = f'"{broken_url}" % {var_name}'
                new = f'"{correct_url}"'
                content = content.replace(old, new)
                print(f"  修复 %s 模式: {var_name}={var_value} -> {filename}")
    return content


def patch_bzl_file(bzl_path):
    """修补单个 bzl 文件"""
    try:
        with open(bzl_path) as f:
            content = f.read()
    except:
        return False

    original = content

    # 1. 先修复 %s 格式字符串问题
    content = fix_percent_s_patterns(content)

    # 2. 替换 GitHub URL 为 file:// URL
    # 匹配引号内的 GitHub 下载 URL
    urls = set(re.findall(r'"(https://github\.com/[^"]+)"', content))
    download_urls = set()
    for url in urls:
        if any(p in url for p in ['/archive/', '/releases/download/', '.tar.gz', '.zip']):
            download_urls.add(url)

    for url in download_urls:
        filename = url_to_filename(url)
        filepath = os.path.join(DEPS_DIR, filename)
        if os.path.exists(filepath):
            file_url = f"file://{DEPS_DIR}/{filename}"
            content = content.replace(f'"{url}"', f'"{file_url}"')

    # 3. 处理 .format(commit = VAR) 模式
    format_matches = re.findall(r'"(https://github\.com/[^"]*\{commit\}[^"]*)"\.format\(commit = (\w+)\)', content)
    for url_template, var_name in format_matches:
        var_match = re.search(rf'{var_name}\s*=\s*"([a-f0-9]+)"', content)
        if var_match:
            commit = var_match.group(1)
            full_url = url_template.replace('{commit}', commit)
            filename = url_to_filename(full_url)
            filepath = os.path.join(DEPS_DIR, filename)
            if os.path.exists(filepath):
                file_url = f"file://{DEPS_DIR}/{filename}"
                old = f'"{url_template}".format(commit = {var_name})'
                new = f'"{file_url}"'
                content = content.replace(old, new)
                if f'"{full_url}"' in content:
                    content = content.replace(f'"{full_url}"', f'"{file_url}"')

    # 4. 处理 % (VAR, VAR) 多参数格式字符串
    # 例如: "https://github.com/.../rules_jvm_external-%s.tar.gz" % (TAG, TAG)
    multi_percent_pattern = r'"(https://github\.com/[^"]*%s[^"]*)"\s*%\s*\(([^)]+)\)'
    multi_matches = re.findall(multi_percent_pattern, content)
    for url_template, var_list in multi_matches:
        var_names = [v.strip() for v in var_list.split(',')]
        # 查找所有变量值
        var_values = []
        for vn in var_names:
            vmatch = re.search(rf'{vn}\s*=\s*"([^"]+)"', content)
            if vmatch:
                var_values.append(vmatch.group(1))
            else:
                break
        if len(var_values) == len(var_names):
            full_url = url_template
            for vv in var_values:
                full_url = full_url.replace('%s', vv, 1)
            filename = url_to_filename(full_url)
            filepath = os.path.join(DEPS_DIR, filename)
            if os.path.exists(filepath):
                file_url = f"file://{DEPS_DIR}/{filename}"
                old = f'"{url_template}" % ({var_list})'
                new = f'"{file_url}"'
                content = content.replace(old, new)

    # 5. 处理单变量 %s 格式字符串
    # 例如: "https://github.com/.../archive/%s.tar.gz" % RULES_BOOST_COMMIT
    single_percent_pattern = r'"(https://github\.com/[^"]*%s[^"]*)"\s*%\s*(\w+)'
    # 排除已被多变量模式匹配的情况
    for m in re.finditer(single_percent_pattern, content):
        url_template = m.group(1)
        var_name = m.group(2)
        # 检查是否是多变量模式（后面跟 ( ）
        full_match = m.group(0)
        if f'% {var_name}' in full_match and not f'% ({var_name}' in full_match:
            var_match = re.search(rf'{var_name}\s*=\s*"([^"]+)"', content)
            if var_match:
                var_value = var_match.group(1)
                full_url = url_template.replace('%s', var_value)
                filename = url_to_filename(full_url)
                filepath = os.path.join(DEPS_DIR, filename)
                if os.path.exists(filepath):
                    file_url = f"file://{DEPS_DIR}/{filename}"
                    old = f'"{url_template}" % {var_name}'
                    new = f'"{file_url}"'
                    content = content.replace(old, new)

    if content != original:
        try:
            with open(bzl_path, 'w') as f:
                f.write(content)
            return True
        except PermissionError:
            # 尝试添加写权限
            os.chmod(bzl_path, 0o644)
            try:
                with open(bzl_path, 'w') as f:
                    f.write(content)
                return True
            except:
                return False
    return False


def main():
    if not os.path.isdir(BAZEL_EXTERNAL):
        print(f"错误: Bazel external 目录不存在: {BAZEL_EXTERNAL}", file=sys.stderr)
        sys.exit(1)

    if not os.path.isdir(DEPS_DIR):
        print(f"错误: deps 目录不存在: {DEPS_DIR}", file=sys.stderr)
        sys.exit(1)

    print(f"=== 扫描 bzl 文件 ===")
    bzl_files = find_bzl_files()
    print(f"  找到 {len(bzl_files)} 个文件")

    patched = 0
    for bzl_path in bzl_files:
        if patch_bzl_file(bzl_path):
            rel_path = os.path.relpath(bzl_path, BAZEL_EXTERNAL)
            print(f"  已修补: {rel_path}")
            patched += 1

    print(f"\n  总共修补 {patched} 个文件")

    # 修补 _github_archive 函数，添加 file:// URL 作为首选
    print("=== 修补 _github_archive 函数 ===")
    github_archive_fixed = 0
    for bzl_path in bzl_files:
        try:
            with open(bzl_path) as f:
                content = f.read()
        except:
            continue
        original = content
        # 查找 _github_archive 函数定义并添加 file:// URL
        if 'def _github_archive(repo, commit' in content and 'file:///secretflow' not in content:
            content = content.replace(
                'urls = [repo + "/archive/" + commit + ".zip"],',
                'urls = [\n            "file:///secretflow/interconnection-impl/.cache/deps/" + commit + ".zip",\n            repo + "/archive/" + commit + ".zip",\n        ],'
            )
        if content != original:
            try:
                with open(bzl_path, 'w') as f:
                    f.write(content)
                rel_path = os.path.relpath(bzl_path, BAZEL_EXTERNAL)
                print(f"  修补 _github_archive: {rel_path}")
                github_archive_fixed += 1
            except:
                pass
    print(f"  修补了 {github_archive_fixed} 个文件中的 _github_archive")

    # 使要求 sha256 作为最后一个参数的函数定义变为可选（添加默认值）
    # 只修补 sha256 是最后一个参数的情况，避免后面的必需参数报错
    print("=== 修补函数定义（使 sha256 可选）===")
    func_fixed = 0
    for bzl_path in bzl_files:
        try:
            with open(bzl_path) as f:
                content = f.read()
        except:
            continue
        original = content
        # 只匹配 sha256 是最后一个参数的情况: def func(..., sha256)
        # 不匹配 sha256 后面还有参数的情况: def func(..., sha256, out_folder)
        content = re.sub(
            r'(def\s+\w+\s*\([^)]*\bsha256)(\s*\))',
            r'\1 = ""\2',
            content
        )
        if content != original:
            try:
                with open(bzl_path, 'w') as f:
                    f.write(content)
                rel_path = os.path.relpath(bzl_path, BAZEL_EXTERNAL)
                print(f"  修补函数定义: {rel_path}")
                func_fixed += 1
            except:
                pass
    print(f"  修补了 {func_fixed} 个文件中的函数定义")

    print("=== 完成 ===")


if __name__ == '__main__':
    main()
