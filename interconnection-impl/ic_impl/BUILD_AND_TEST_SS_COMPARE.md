# SS-Compare 算法构建和测试指南

## 问题诊断

如果遇到 `Create algo handler failed` 错误，通常是因为：
1. Protobuf 文件未编译，`ALGO_TYPE_SS_COMPARE` 枚举值不存在
2. 项目未重新编译，新代码未生效
3. 环境变量中的算法名称不正确

## 构建步骤

### 1. 编译 Protobuf

首先需要编译 interconnection 仓库中的 protobuf 文件：

```bash
cd /root/project/secretflow/interconnection
bash build_proto.sh
```

或者手动编译：

```bash
cd /root/project/secretflow/interconnection
bazel build //interconnection:ic_py_proto
```

### 2. 编译 interconnection-impl

编译包含 SS-Compare 算法的项目：

```bash
cd /root/project/secretflow/interconnection-impl
bazel build -c opt ic_impl/ic_main
```

### 3. 验证编译

检查编译后的文件：

```bash
# 检查可执行文件
ls -lh bazel-bin/ic_impl/ic_main

# 检查 protobuf 是否包含 SS_COMPARE
grep -r "ALGO_TYPE_SS_COMPARE" bazel-bin 2>/dev/null | head -3
```

## 测试步骤

### 方式1: 使用测试脚本（推荐）

```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
bash test_ss_compare_local.sh
```

### 方式2: 手动运行两个终端

#### 终端1: 运行 Alice (rank 0, passive)
```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
source env/ss-compare-env-alice.sh
./run_ic.sh ss-compare alice
```

#### 终端2: 运行 Bob (rank 1, active)
```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
source env/ss-compare-env-bob.sh
./run_ic.sh ss-compare bob
```

### 方式3: 使用 run_ic.sh 脚本

```bash
# 终端1
cd /root/project/secretflow/interconnection-impl/ic_impl
./run_ic.sh ss-compare alice

# 终端2
cd /root/project/secretflow/interconnection-impl/ic_impl
./run_ic.sh ss-compare bob
```

## 输入输出说明

### 输入数据

- **Alice**: `data/perfect_logit_a.csv`
  - 11列: x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, y
  - 全局列索引: 0-10
  - 样本数: 10000行（不包括header）

- **Bob**: `data/perfect_logit_b.csv`
  - 10列: x11, x12, x13, x14, x15, x16, x17, x18, x19, x20
  - 全局列索引: 11-20
  - 样本数: 10000行（不包括header）

### 输出数据

- **Alice**: `output/result-ss-compare-alice.csv`
  - 格式: 每行一个布尔值（true/false）
  - 内容: 表示该样本的指定列（compare_column_idx）是否满足比较条件（compare_op）
  - 行数: 10000行（与输入样本数相同）

- **Bob**: `output/result-ss-compare-bob.csv`
  - 格式: 每行一个布尔值（true/false）
  - 内容: 与Alice输出相同（因为是reveal后的结果）
  - 行数: 10000行（与输入样本数相同）

### 配置参数

- `compare_column_idx=0`: 比较第一列（Alice的x1列，全局列索引0）
- `compare_op=0`: 大于操作（比较该列的值是否大于该列的均值）
- `feature_nums={"host.0":11, "guest.0":10}`: Alice有11列，Bob有10列

## 算法流程

1. **数据加载**: 各方加载本地CSV文件
2. **数据编码**: 将数据编码为秘密分享格式（Semi2K协议）
3. **数据拼接**: 将各方数据横向拼接（Alice的11列 + Bob的10列 = 21列）
4. **列选择**: 根据 `compare_column_idx` 选择要比较的列（全局列索引）
5. **阈值计算**: 计算该列所有样本的均值作为阈值
6. **安全比较**: 在SPU上执行安全比较操作
   - 对每一行：比较该列的值与阈值
   - 使用 `less` 操作实现所有比较类型
7. **结果输出**: 将比较结果reveal并输出为true/false

## 验证结果

### 检查输出文件

```bash
# 检查文件是否存在
ls -lh output/result-ss-compare-*.csv

# 查看前10行
head -10 output/result-ss-compare-alice.csv

# 统计true和false的数量
grep -o "true" output/result-ss-compare-alice.csv | wc -l
grep -o "false" output/result-ss-compare-alice.csv | wc -l

# 验证格式（应该只有true和false）
grep -vE "^(true|false)$" output/result-ss-compare-alice.csv
# 如果没有输出，说明格式正确
```

### 检查日志

```bash
# 查看Alice日志
tail -50 logs/ss-compare/alice/ic_main-*.log

# 查看Bob日志
tail -50 logs/ss-compare/bob/ic_main-*.log

# 检查错误
grep -i "error\|fail\|exception" logs/ss-compare/*/ic_main-*.log
```

## 故障排查

### 问题1: Create algo handler failed

**原因**: Protobuf未编译或项目未重新编译

**解决**:
```bash
# 1. 编译protobuf
cd /root/project/secretflow/interconnection
bazel build //interconnection:ic_py_proto

# 2. 重新编译项目
cd /root/project/secretflow/interconnection-impl
bazel build -c opt ic_impl/ic_main

# 3. 验证
grep -r "ALGO_TYPE_SS_COMPARE" bazel-bin 2>/dev/null
```

### 问题2: 列索引越界

**原因**: `compare_column_idx` 超出了有效范围

**解决**: 
- 检查 `compare_column_idx` 是否在 0-20 范围内
- 验证 `feature_nums` 是否正确（Alice:11, Bob:10）

### 问题3: 样本数量不一致

**原因**: 双方数据文件的样本数量不同

**解决**: 
- 确保双方数据文件的行数相同（不包括header）
- 检查数据文件格式

### 问题4: 输出文件为空

**原因**: 算法执行失败

**解决**: 
- 检查日志文件中的错误信息
- 验证SPU上下文是否正确初始化
- 检查数据文件是否可以正常读取

## 示例输出

### 输出文件示例

```
true
false
true
true
false
true
false
false
true
true
...
```

### 日志输出示例

```
[2025-11-10 13:50:41.074] [info] **********run SS-Compare**********
[2025-11-10 13:50:41.074] [info] [SS-Compare] Loading dataset...
[2025-11-10 13:50:41.074] [info] [SS-Compare] Dataset loaded: 10000 samples
[2025-11-10 13:50:41.074] [info] [SS-Compare] Starting secure comparison...
[2025-11-10 13:50:45.123] [info] [SS-Compare] comparison result saved to output/result-ss-compare-alice.csv (10000 rows)
```

