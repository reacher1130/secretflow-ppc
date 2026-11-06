# SS-Compare 快速开始指南

## 问题修复

### 1. 取消注释代码
已修复 `party.cc` 和 `BUILD.bazel` 中被注释的代码，现在SS-Compare算法已正确注册。

### 2. 重新编译
由于代码修改，需要重新编译项目：

```bash
cd /root/project/secretflow/interconnection-impl
bazel build //ic_impl:ic_main
```

## 测试步骤

### 方式1: 使用测试脚本（推荐）

```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
bash run_ss_compare_test.sh
```

### 方式2: 手动启动两个终端

#### 终端1: 启动Alice
```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
./run_ic.sh ss-compare alice
```

#### 终端2: 启动Bob
```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
./run_ic.sh ss-compare bob
```

## 预期输出

### 输出文件
- `output/result-ss-compare-alice.csv`
- `output/result-ss-compare-bob.csv`

### 输出格式
每行一个布尔值（true/false），例如：
```
true
false
true
true
false
...
```

## 配置说明

### 当前配置
- **比较列**: `compare_column_idx=0` (Alice的第一列 x1)
- **比较操作**: `compare_op=0` (大于)
- **阈值**: 该列的均值
- **结果**: 每行表示该样本的x1值是否大于该列的均值

### 修改配置
编辑环境脚本 `env/ss-compare-env-alice.sh` 和 `env/ss-compare-env-bob.sh`：

```bash
# 修改比较列（全局列索引）
"runtime.component.parameter.compare_column_idx=0"  # 0-10: Alice的列, 11-20: Bob的列

# 修改比较操作
"runtime.component.parameter.compare_op=0"  # 0=大于, 1=小于, 2=等于, 3=大于等于, 4=小于等于
```

## 验证结果

### 检查输出文件
```bash
# 查看输出
head -20 output/result-ss-compare-alice.csv
head -20 output/result-ss-compare-bob.csv

# 验证格式
grep -vE "^(true|false)$" output/result-ss-compare-alice.csv
# 如果没有输出，说明格式正确

# 统计true/false数量
grep -c "true" output/result-ss-compare-alice.csv
grep -c "false" output/result-ss-compare-alice.csv
```

### 检查日志
```bash
# 查看最新日志
ls -lt logs/ss-compare/alice/ | head -1
ls -lt logs/ss-compare/bob/ | head -1

# 查看日志内容
tail -50 logs/ss-compare/alice/ic_main-*.log
tail -50 logs/ss-compare/bob/ic_main-*.log
```

## 故障排查

### 问题1: 编译错误
**解决**: 确保所有依赖都已正确配置，重新编译：
```bash
bazel clean
bazel build //ic_impl:ic_main
```

### 问题2: 运行时错误 "Create algo handler failed"
**原因**: 代码未正确编译或注册
**解决**: 
1. 检查 `party.cc` 中SS-Compare的代码是否取消注释
2. 检查 `BUILD.bazel` 中factory_ss_compare.cc是否取消注释
3. 重新编译项目

### 问题3: 列索引越界
**解决**: 检查 `compare_column_idx` 是否在有效范围内（0-20）

### 问题4: 输出文件为空
**解决**: 
1. 检查日志文件中的错误信息
2. 验证数据文件是否存在
3. 检查SPU上下文是否正确初始化

## 算法说明

### 比较逻辑
1. 加载各方数据
2. 选择指定列（`compare_column_idx`）
3. 计算该列的均值作为阈值
4. 对每一行的值执行比较操作
5. 输出布尔结果（true/false）

### 比较操作
- **大于 (0)**: `value > mean`
- **小于 (1)**: `value < mean`
- **等于 (2)**: `|value - mean| < epsilon`
- **大于等于 (3)**: `value >= mean`
- **小于等于 (4)**: `value <= mean`

## 下一步

1. 重新编译项目
2. 运行测试脚本
3. 检查输出结果
4. 根据需要进行配置调整

