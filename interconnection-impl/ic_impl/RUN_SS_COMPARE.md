# SS-Compare 算法运行指南

## 前置条件

### 1. 重新编译项目

由于添加了新算法，需要重新编译：

```bash
cd /root/project/secretflow/interconnection-impl

# 编译 protobuf（如果还没有编译）
cd ../interconnection
bazel build //interconnection:ic_py_proto

# 编译项目
cd ../interconnection-impl
bazel build -c opt ic_impl/ic_main
```

### 2. 检查数据文件

确保数据文件存在：
```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
ls -lh data/perfect_logit_a.csv data/perfect_logit_b.csv
```

## 运行方式

### 方式1: 使用 run_ic.sh 脚本（推荐）

**终端1 - Alice (rank 0, passive)**:
```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
./run_ic.sh ss-compare alice
```

**终端2 - Bob (rank 1, active)**:
```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
./run_ic.sh ss-compare bob
```

### 方式2: 直接运行（手动）

**终端1 - Alice**:
```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
source env/ss-compare-env-alice.sh
/path/to/bazel-bin/ic_impl/ic_main --disable_handshake
```

**终端2 - Bob**:
```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
source env/ss-compare-env-bob.sh
/path/to/bazel-bin/ic_impl/ic_main --disable_handshake
```

### 方式3: 使用测试脚本

```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
bash test_ss_compare_local.sh
```

## 输入输出说明

### 输入数据

**Alice (perfect_logit_a.csv)**:
- 11列: x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, y
- 全局列索引: 0-10
- 样本数: 10000行（不包括header）

**Bob (perfect_logit_b.csv)**:
- 10列: x11, x12, x13, x14, x15, x16, x17, x18, x19, x20
- 全局列索引: 11-20
- 样本数: 10000行（不包括header）

### 输出数据

**Alice (result-ss-compare-alice.csv)**:
- 格式: 每行一个布尔值（true/false）
- 内容: 表示该样本的指定列是否满足比较条件
- 行数: 10000行

**Bob (result-ss-compare-bob.csv)**:
- 格式: 每行一个布尔值（true/false）
- 内容: 与Alice输出相同
- 行数: 10000行

### 配置参数

- `compare_column_idx=0`: 比较第一列（Alice的x1列，全局列索引0）
- `compare_op=0`: 大于操作（比较该列的值是否大于该列的均值）
- `feature_nums={"host.0":11, "guest.0":10}`: Alice有11列，Bob有10列

## 算法流程

1. **数据加载**: 各方加载本地CSV文件
2. **数据编码**: 将数据编码为秘密分享格式
3. **数据拼接**: 将各方数据横向拼接（21列）
4. **列选择**: 选择要比较的列（compare_column_idx=0，即第一列）
5. **阈值计算**: 计算该列的均值作为阈值
6. **安全比较**: 在SPU上执行安全比较（每行的值 vs 阈值）
7. **结果输出**: 将比较结果reveal并输出为true/false

## 验证结果

### 检查输出文件

```bash
# 检查文件是否存在
ls -lh output/result-ss-compare-*.csv

# 查看前20行
head -20 output/result-ss-compare-alice.csv

# 统计true和false的数量
echo "True count: $(grep -o "true" output/result-ss-compare-alice.csv | wc -l)"
echo "False count: $(grep -o "false" output/result-ss-compare-alice.csv | wc -l)"

# 验证格式
grep -vE "^(true|false)$" output/result-ss-compare-alice.csv
# 如果没有输出，说明格式正确
```

### 检查日志

```bash
# 查看最新日志
tail -50 logs/ss-compare/alice/latest.log
tail -50 logs/ss-compare/bob/latest.log

# 检查错误
grep -i "error\|fail\|exception" logs/ss-compare/*/latest.log
```

## 故障排查

### 问题: Create algo handler failed

**原因**: 项目未重新编译或protobuf未编译

**解决**:
```bash
# 1. 编译protobuf
cd /root/project/secretflow/interconnection
bazel build //interconnection:ic_py_proto

# 2. 重新编译项目
cd ../interconnection-impl
bazel build -c opt ic_impl/ic_main

# 3. 验证
grep -r "ALGO_TYPE_SS_COMPARE" bazel-bin 2>/dev/null | head -1
```

### 问题: 列索引越界

**原因**: compare_column_idx 超出有效范围

**解决**: 检查 compare_column_idx 是否在 0-20 范围内

### 问题: 样本数量不一致

**原因**: 双方数据文件的行数不同

**解决**: 确保双方数据文件的行数相同

## 示例输出

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

## 完整测试流程

```bash
# 1. 清理
pkill -f "ic_main.*ss-compare" 2>/dev/null || true
rm -f output/result-ss-compare-*.csv

# 2. 终端1: 启动Alice
cd /root/project/secretflow/interconnection-impl/ic_impl
./run_ic.sh ss-compare alice

# 3. 终端2: 启动Bob（等待终端1启动后）
cd /root/project/secretflow/interconnection-impl/ic_impl
./run_ic.sh ss-compare bob

# 4. 等待完成，检查结果
ls -lh output/result-ss-compare-*.csv
head -20 output/result-ss-compare-alice.csv
```

