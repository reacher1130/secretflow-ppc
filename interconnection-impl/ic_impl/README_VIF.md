# SS-VIF 算法使用说明

## 概述

SS-VIF (Secure Shared Variance Inflation Factor) 是一个基于秘密分享的方差膨胀因子计算算法，用于检测特征之间的多重共线性。

## VIF 值说明

- **VIF 值范围**：VIF 值应该是非负数，通常范围在 1 到几十之间
- **VIF < 1**：表示特征之间几乎没有相关性
- **1 ≤ VIF ≤ 10**：表示特征之间存在轻微的多重共线性（可接受）
- **10 < VIF ≤ 100**：表示特征之间存在中等的多重共线性（需要注意）
- **VIF > 100**：表示特征之间存在严重的多重共线性（建议移除相关特征）
- **VIF > 1000**：表示特征之间可能存在完全的多重共线性或数值问题

## 使用方法

### 1. 运行算法

```bash
# 终端1：启动 Alice
cd /root/project/secretflow/interconnection-impl/ic_impl
bash run_ic.sh ss-vif alice

# 终端2：启动 Bob
cd /root/project/secretflow/interconnection-impl/ic_impl
bash run_ic.sh ss-vif bob
```

或者使用测试脚本：

```bash
./test_ss_vif.sh
```

### 2. 合并份额得到真实 VIF 值

在秘密分享模式下，每个参与方输出的是 VIF 值的**份额（share）**，需要将所有参与方的份额相加才能得到真实的 VIF 值。

```bash
# 合并所有参与方的输出
python3 merge_vif_results.py output/result-ss-vif-*.csv

# 或者指定文件
python3 merge_vif_results.py output/result-ss-vif-alice.csv output/result-ss-vif-bob.csv
```

合并后的真实 VIF 值会保存到 `output/result-vif-merged.csv`。

### 3. 解释结果

合并工具会自动验证 VIF 值的合理性：
- ✓ 所有 VIF 值都是非负数
- ⚠️ 标记存在严重多重共线性的特征（VIF > 100）
- ⚠️ 标记存在中等多重共线性的特征（10 < VIF ≤ 100）

## 输出文件说明

- `output/result-ss-vif-alice.csv`: Alice 的 VIF 份额
- `output/result-ss-vif-bob.csv`: Bob 的 VIF 份额
- `output/result-vif-merged.csv`: 合并后的真实 VIF 值（由合并工具生成）

## 注意事项

1. **份额 vs 真实值**：单独查看某个参与方的输出文件时，可能会看到负数，这是正常的（因为这是份额）。真实的 VIF 值（所有份额之和）应该都是非负数。

2. **数值精度**：由于使用了牛顿迭代法进行矩阵逆计算，结果可能有一些数值误差，但通常在可接受范围内。

3. **多重共线性检测**：如果发现很多特征的 VIF 值都很大（>100），说明数据存在严重的多重共线性问题，建议：
   - 检查特征之间是否存在线性关系
   - 考虑使用特征选择或降维技术
   - 移除高度相关的特征

## 配置参数

可以通过环境变量配置以下参数：

- `vif_newton_iter`: 牛顿迭代的迭代次数（默认：20）
- `vif_clip`: VIF 值的截断阈值（默认：-1，表示不截断）
- `result_to_rank`: 指定输出结果的参与方（-1 表示所有参与方都输出）

## 算法实现

SS-VIF 算法的实现步骤：

1. **数据标准化**：对输入数据进行中心化和标准化
2. **计算 X^T X**：计算标准化后数据的协方差矩阵
3. **牛顿迭代求逆**：使用牛顿迭代法计算矩阵的逆
4. **提取对角线**：提取逆矩阵的对角线元素
5. **计算 VIF**：VIF = diag((X^T X)^{-1}) * (n-1)

## 参考文献

- [Variance Inflation Factor - Wikipedia](https://en.wikipedia.org/wiki/Variance_inflation_factor)
- [SecretFlow SS-VIF Implementation](https://github.com/secretflow/secretflow/blob/main/secretflow/stats/ss_vif_v.py)
