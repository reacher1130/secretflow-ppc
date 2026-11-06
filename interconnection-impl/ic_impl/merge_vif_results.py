#!/usr/bin/env python3
"""
合并所有参与方的 VIF 份额，得到真实的 VIF 值

在秘密分享模式下，每个参与方输出的是 VIF 值的份额（share），
需要将所有参与方的份额相加才能得到真实的 VIF 值。

用法:
    python3 merge_vif_results.py output/result-ss-vif-alice.csv output/result-ss-vif-bob.csv
    或者
    python3 merge_vif_results.py output/result-ss-vif-*.csv
"""

import sys
import numpy as np
import glob
import os

def merge_vif_shares(file_paths):
    """合并多个 VIF 份额文件，得到真实的 VIF 值"""
    if not file_paths:
        print("错误：请提供至少一个 VIF 份额文件", file=sys.stderr)
        return None
    
    # 读取所有份额文件
    shares = []
    for file_path in file_paths:
        if not os.path.exists(file_path):
            print(f"警告：文件不存在: {file_path}", file=sys.stderr)
            continue
        try:
            share = np.loadtxt(file_path)
            shares.append(share)
            print(f"读取份额文件: {file_path}, 形状: {share.shape}")
        except Exception as e:
            print(f"错误：无法读取文件 {file_path}: {e}", file=sys.stderr)
            continue
    
    if not shares:
        print("错误：没有有效的份额文件", file=sys.stderr)
        return None
    
    # 检查所有份额的形状是否一致
    shapes = [s.shape for s in shares]
    if len(set(shapes)) > 1:
        print(f"错误：份额形状不一致: {shapes}", file=sys.stderr)
        return None
    
    # 合并份额（相加）
    true_vif = np.sum(shares, axis=0)
    
    return true_vif

def validate_vif(vif_values):
    """验证 VIF 值的合理性"""
    print("\n=== VIF 值验证 ===")
    print(f"特征数量: {len(vif_values)}")
    print(f"最小值: {vif_values.min():.6f}")
    print(f"最大值: {vif_values.max():.6f}")
    print(f"平均值: {vif_values.mean():.6f}")
    print(f"中位数: {np.median(vif_values):.6f}")
    
    # 检查负数
    negative_count = np.sum(vif_values < 0)
    if negative_count > 0:
        print(f"⚠️  警告：发现 {negative_count} 个负数值（VIF 不应该是负数）")
        print(f"   负数值索引: {np.where(vif_values < 0)[0]}")
    else:
        print("✓ 所有 VIF 值都是非负数")
    
    # 检查异常大的值
    very_large_count = np.sum(vif_values > 1000)
    if very_large_count > 0:
        print(f"⚠️  注意：发现 {very_large_count} 个非常大的值（>1000），可能表示严重的多重共线性")
        large_indices = np.where(vif_values > 1000)[0]
        print(f"   大值索引: {large_indices}")
        print(f"   大值: {vif_values[large_indices]}")
    
    # 检查合理的值范围
    reasonable_count = np.sum((vif_values >= 1) & (vif_values <= 10))
    print(f"✓ 合理范围内的值 (1-10): {reasonable_count} / {len(vif_values)}")
    
    moderate_count = np.sum((vif_values > 10) & (vif_values <= 100))
    if moderate_count > 0:
        print(f"⚠️  中等多重共线性 (10-100): {moderate_count} / {len(vif_values)}")
    
    severe_count = np.sum(vif_values > 100)
    if severe_count > 0:
        print(f"⚠️  严重多重共线性 (>100): {severe_count} / {len(vif_values)}")
    
    return {
        'negative_count': negative_count,
        'very_large_count': very_large_count,
        'reasonable_count': reasonable_count,
        'moderate_count': moderate_count,
        'severe_count': severe_count,
    }

def main():
    if len(sys.argv) < 2:
        print("用法: python3 merge_vif_results.py <share_file1> <share_file2> ...")
        print("或者: python3 merge_vif_results.py output/result-ss-vif-*.csv")
        sys.exit(1)
    
    # 处理通配符
    file_paths = []
    for arg in sys.argv[1:]:
        if '*' in arg:
            file_paths.extend(glob.glob(arg))
        else:
            file_paths.append(arg)
    
    file_paths = sorted(set(file_paths))  # 去重并排序
    
    print(f"合并 {len(file_paths)} 个份额文件:")
    for fp in file_paths:
        print(f"  - {fp}")
    
    # 合并份额
    true_vif = merge_vif_shares(file_paths)
    if true_vif is None:
        sys.exit(1)
    
    # 验证结果
    stats = validate_vif(true_vif)
    
    # 输出结果
    output_file = "output/result-vif-merged.csv"
    np.savetxt(output_file, true_vif, fmt='%.6f')
    print(f"\n✓ 真实的 VIF 值已保存到: {output_file}")
    
    # 输出详细结果
    print("\n=== 详细的 VIF 值 ===")
    for i, vif in enumerate(true_vif):
        status = ""
        if vif < 0:
            status = " (异常: 负数)"
        elif vif > 100:
            status = " (严重多重共线性)"
        elif vif > 10:
            status = " (中等多重共线性)"
        elif vif >= 1:
            status = " (正常)"
        else:
            status = " (低相关性)"
        print(f"特征 {i+1}: {vif:.6f}{status}")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())

