# SS-Compare 算法测试指南

## 概述

SS-Compare 是基于秘密分享的安全比较算法，支持选择某一列进行比较，输出 true/false 布尔结果。

## 配置说明

### 环境变量配置

#### 输入输出
- `runtime.component.input.train_data`: 输入数据文件（CSV格式）
- `runtime.component.output.train_data`: 输出结果文件（CSV格式，包含true/false）

#### 算法参数
- `runtime.component.parameter.algo=ss_compare`: 算法类型
- `runtime.component.parameter.compare_column_idx`: 要比较的列索引（全局列索引）
  - 0: 第一列（Alice的第一列）
  - 10: 第11列（如果Alice有10列，这是Bob的第一列）
- `runtime.component.parameter.compare_op`: 比较操作类型
  - 0: 大于 (greater than)
  - 1: 小于 (less than)
  - 2: 等于 (equal to)
  - 3: 大于等于 (greater than or equal to)
  - 4: 小于等于 (less than or equal to)

#### 数据参数
- `runtime.component.parameter.feature_nums`: 各方特征数量
  - 格式: `{"host.0":10, "guest.0":10}`
  - 需要匹配实际数据文件的列数（不包括header）

#### 协议参数
- `runtime.component.parameter.protocol_families=ss`: 协议族
- `runtime.component.parameter.protocol=semi2k`: 协议类型
- `runtime.component.parameter.field=64`: 字段类型
- `runtime.component.parameter.fxp_bits=18`: 定点数位数
- `runtime.component.parameter.trunc_mode=probabilistic`: 截断模式

## 数据文件格式

### 输入数据
- CSV格式，第一行为表头
- 数据为浮点数，逗号分隔
- 示例：
  ```
  x1,x2,x3,x4,x5,x6,x7,x8,x9,x10
  0.5,0.3,0.8,0.2,0.9,0.1,0.6,0.4,0.7,0.3
  ...
  ```

### 输出数据
- CSV格式，每行一个布尔值（true/false）
- 示例：
  ```
  true
  false
  true
  ...
  ```

## 测试步骤

### 1. 准备数据文件
确保数据文件存在：
- `data/perfect_logit_a.csv` (Alice的数据)
- `data/perfect_logit_b.csv` (Bob的数据)

### 2. 配置环境变量
检查环境脚本：
- `env/ss-compare-env-alice.sh`
- `env/ss-compare-env-bob.sh`

关键配置：
- `compare_column_idx=0`: 比较第一列
- `compare_op=0`: 使用大于比较
- `feature_nums`: 匹配实际数据列数

### 3. 运行测试
```bash
# 方式1: 使用测试脚本
cd /root/project/secretflow/interconnection-impl/ic_impl
bash test_ss_compare.sh

# 方式2: 手动运行
# 终端1: 运行Alice
bash run_ic.sh ss-compare alice

# 终端2: 运行Bob
bash run_ic.sh ss-compare bob
```

### 4. 检查结果
输出文件：
- `output/result-ss-compare-alice.csv`
- `output/result-ss-compare-bob.csv`

每个文件包含：
- 每行一个布尔值（true/false）
- 表示该样本的指定列是否满足比较条件

## 算法逻辑

1. **数据加载**: 各方加载本地CSV文件
2. **数据编码**: 将数据编码为秘密分享格式
3. **列选择**: 根据 `compare_column_idx` 选择要比较的列
4. **阈值计算**: 计算该列的均值作为阈值
5. **安全比较**: 在SPU上执行安全比较操作
6. **结果输出**: 将比较结果reveal并输出为true/false

## 注意事项

1. **列索引**: `compare_column_idx` 是全局列索引
   - 如果Alice有10列，Bob有10列
   - 列索引0-9: Alice的列
   - 列索引10-19: Bob的列

2. **特征数量**: `feature_nums` 必须匹配实际数据文件的列数
   - 不包括header行
   - 如果数据文件有标签列，需要相应调整

3. **比较操作**: 当前实现使用列均值作为阈值
   - 可以修改为其他阈值（如固定值、中位数等）

4. **输出格式**: 输出为布尔值文本（true/false）
   - 每行对应一个样本
   - 双方输出相同的比较结果

## 故障排查

### 问题1: 列索引越界
- 检查 `compare_column_idx` 是否在有效范围内
- 验证 `feature_nums` 是否正确

### 问题2: 样本数量不一致
- 确保双方数据文件的样本数量相同
- 检查数据文件格式

### 问题3: 输出文件为空
- 检查日志文件中的错误信息
- 验证SPU上下文是否正确初始化

### 问题4: 结果全为true或false
- 检查比较操作类型是否正确
- 验证阈值计算是否正确

## 示例

### 示例1: 比较第一列是否大于均值
```bash
# 环境变量设置
compare_column_idx=0
compare_op=0  # 大于
```

### 示例2: 比较第11列是否小于均值
```bash
# 环境变量设置
compare_column_idx=10  # Bob的第一列
compare_op=1  # 小于
```

## 参考

- 其他算法的测试脚本: `test_ss_vif.sh`, `test_ss_pearson.sh`
- 环境脚本示例: `env/ss-vif-env-alice.sh`
- 输出示例: `output/result-ss-vif-alice.csv`

