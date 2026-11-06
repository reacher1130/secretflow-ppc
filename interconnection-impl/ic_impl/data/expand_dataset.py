#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
扩充数据集到千万级
"""
import pandas as pd
import numpy as np
import os
from tqdm import tqdm

def expand_dataset(input_file_a, input_file_b, output_file_a, output_file_b, target_rows=10000000):
    """
    随机扩充数据集到指定行数（使用分批写入以节省内存）
    
    Args:
        input_file_a: 输入文件A路径
        input_file_b: 输入文件B路径
        output_file_a: 输出文件A路径
        output_file_b: 输出文件B路径
        target_rows: 目标行数（不包括表头）
    """
    print(f"正在读取原始数据...")
    # 读取原始数据
    df_a = pd.read_csv(input_file_a)
    df_b = pd.read_csv(input_file_b)
    
    original_rows = len(df_a)
    print(f"原始数据行数: {original_rows}")
    print(f"目标行数: {target_rows}")
    
    # 计算需要扩充的倍数
    multiplier = target_rows // original_rows
    remainder = target_rows % original_rows
    
    print(f"扩充倍数: {multiplier}, 余数: {remainder}")
    
    # 打开输出文件并写入表头
    print(f"正在扩充数据A...")
    df_a.head(0).to_csv(output_file_a, index=False, float_format='%.15e')
    
    # 分批写入数据A
    written_rows_a = 0
    for i in tqdm(range(multiplier), desc="批量复制A"):
        # 添加小的随机噪声（保持数据分布特征）
        noise = np.random.normal(0, 0.01, df_a.shape)
        noisy_data = df_a.values + noise
        df_batch = pd.DataFrame(noisy_data, columns=df_a.columns)
        # 追加模式写入
        df_batch.to_csv(output_file_a, mode='a', header=False, index=False, float_format='%.15e')
        written_rows_a += len(df_batch)
    
    # 处理余数部分
    if remainder > 0:
        sampled = df_a.sample(n=remainder, replace=True)
        noise = np.random.normal(0, 0.01, sampled.shape)
        noisy_data = sampled.values + noise
        df_batch = pd.DataFrame(noisy_data, columns=df_a.columns)
        df_batch.to_csv(output_file_a, mode='a', header=False, index=False, float_format='%.15e')
        written_rows_a += len(df_batch)
    
    # 扩充数据B（使用相同的随机种子确保行数一致）
    print(f"正在扩充数据B...")
    df_b.head(0).to_csv(output_file_b, index=False, float_format='%.15e')
    
    # 使用相同的随机种子
    np.random.seed(42)
    written_rows_b = 0
    for i in tqdm(range(multiplier), desc="批量复制B"):
        noise = np.random.normal(0, 0.01, df_b.shape)
        noisy_data = df_b.values + noise
        df_batch = pd.DataFrame(noisy_data, columns=df_b.columns)
        df_batch.to_csv(output_file_b, mode='a', header=False, index=False, float_format='%.15e')
        written_rows_b += len(df_batch)
    
    if remainder > 0:
        sampled = df_b.sample(n=remainder, replace=True, random_state=42)
        noise = np.random.normal(0, 0.01, sampled.shape)
        noisy_data = sampled.values + noise
        df_batch = pd.DataFrame(noisy_data, columns=df_b.columns)
        df_batch.to_csv(output_file_b, mode='a', header=False, index=False, float_format='%.15e')
        written_rows_b += len(df_batch)
    
    # 确保两个数据集行数一致（取较小值）
    final_rows = min(written_rows_a, written_rows_b)
    
    # 如果行数不一致，需要截断较长的文件
    if written_rows_a != written_rows_b:
        print(f"警告：两个文件行数不一致，将截断到 {final_rows} 行")
        # 重新读取并截断
        df_final_a = pd.read_csv(output_file_a, nrows=final_rows)
        df_final_b = pd.read_csv(output_file_b, nrows=final_rows)
        df_final_a.to_csv(output_file_a, index=False, float_format='%.15e')
        df_final_b.to_csv(output_file_b, index=False, float_format='%.15e')
    
    print("完成！")
    print(f"输出文件A: {output_file_a} ({final_rows} 行)")
    print(f"输出文件B: {output_file_b} ({final_rows} 行)")

if __name__ == "__main__":
    # 设置文件路径
    base_dir = os.path.dirname(os.path.abspath(__file__))
    input_file_a = os.path.join(base_dir, "perfect_logit_a.csv")
    input_file_b = os.path.join(base_dir, "perfect_logit_b.csv")
    output_file_a = os.path.join(base_dir, "perfect_logit_a_10m.csv")
    output_file_b = os.path.join(base_dir, "perfect_logit_b_10m.csv")
    
    # 扩充到千万级（10,000,000行）
    expand_dataset(input_file_a, input_file_b, output_file_a, output_file_b, target_rows=10000000)

