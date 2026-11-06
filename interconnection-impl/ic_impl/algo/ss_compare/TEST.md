# SS-Compare 算法测试指南

## 概述

SS-Compare 是一个基于秘密分享的安全比较算法，支持在多方数据上安全地执行比较操作并输出布尔结果。

## 测试环境准备

### 1. 数据文件

测试使用的数据文件：
- **Alice (rank 0)**: `data/perfect_logit_a.csv`
  - 特征列：x1, x2, x3, x4, x5, x6, x7, x8 (共8列)
  - 样本数：10000行（不含表头）
  
- **Bob (rank 1)**: `data/perfect_logit_b.csv`
  - 特征列：x11, x12, x13, x14, x15, x16, x17 (共7列，实际使用 x14-x20)
  - 样本数：10000行（不含表头）

### 2. 数据格式

CSV 文件格式：
- 第一行：表头（列名，逗号分隔）
- 后续行：数据值（浮点数，逗号分隔）

示例：
```
x1,x2,x3,x4,x5,x6,x7,x8
-5.14226436614990e-01,7.30010390281677e-01,-7.30391204357147e-01,...
```

## 配置参数

### 环境变量参数

在 `env/ss-compare-env-alice.sh` 和 `env/ss-compare-env-bob.sh` 中配置：

| 参数 | 说明 | 示例值 |
|------|------|--------|
| `compare_column_idx` | 要比较的列索引（本地列索引，从0开始） | `0` |
| `compare_op` | 比较操作类型 | `0` (大于) |
| `feature_nums` | 各方特征数量 | `{"host.0":8, "guest.0":7}` |
| `result_to_rank` | 结果接收方 | `-1` (所有方都输出) |

### 比较操作类型

| 值 | 操作 | 说明 |
|----|------|------|
| 0 | 大于 (>) | 比较值是否大于阈值 |
| 1 | 小于 (<) | 比较值是否小于阈值 |
| 2 | 等于 (==) | 比较值是否等于阈值 |
| 3 | 大于等于 (>=) | 比较值是否大于等于阈值 |
| 4 | 小于等于 (<=) | 比较值是否小于等于阈值 |

## 运行测试

### 方法1：使用测试脚本（推荐）

```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
./test_ss_compare.sh
```

### 方法2：手动运行

**终端1 - Alice (rank 0):**
```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
bash run_ic.sh ss-compare alice
```

**终端2 - Bob (rank 1):**
```bash
cd /root/project/secretflow/interconnection-impl/ic_impl
bash run_ic.sh ss-compare bob
```

## 输出结果

### 输出文件

算法会在 `output/` 目录下生成以下文件：

- `output/result_rank0.csv` - Alice 的输出结果
- `output/result_rank1.csv` - Bob 的输出结果

### 输出格式

每行一个布尔值（true 或 false），表示对应样本的比较结果。

示例输出：
```
true
false
true
true
false
...
```

### 输出说明

- **列选择**：算法会选择指定的列（通过 `compare_column_idx` 配置）
- **阈值计算**：阈值是该列的均值（所有参与方的数据合并后计算）
- **比较结果**：每一行的值与该阈值进行比较，输出布尔结果
- **双方输出**：Alice 和 Bob 都会输出相同的结果（因为比较是在秘密分享上执行的）

## 验证结果

### 1. 检查输出文件是否存在

```bash
ls -lh output/result_rank*.csv
```

### 2. 查看输出内容

```bash
# 查看前10行
head -10 output/result_rank0.csv

# 统计 true 和 false 的数量
grep -o "true" output/result_rank0.csv | wc -l
grep -o "false" output/result_rank0.csv | wc -l
```

### 3. 验证输出格式

输出应该：
- 每行一个值（true 或 false）
- 行数等于输入样本数（10000行）
- 只包含 true 或 false，没有其他内容

## 常见问题

### 1. 列索引超出范围

如果 `compare_column_idx` 超出了实际列数，算法会报错。确保：
- Alice: `compare_column_idx` 在 0-7 范围内
- Bob: `compare_column_idx` 在 0-6 范围内
- 全局列索引 = Alice列数 + Bob列数 = 8 + 7 = 15（索引 0-14）

### 2. 样本数不一致

确保 Alice 和 Bob 的数据文件具有相同的行数（样本数）。

### 3. 连接失败

检查网络配置和端口设置：
- Alice: `192.168.8.156:9972`
- Bob: `192.168.8.221:9973`

如果是在同一台机器上测试，可能需要修改 IP 地址为 `127.0.0.1` 并使用不同的端口。

## 示例：比较不同列

### 比较 Alice 的第3列（x3，全局列索引2）

修改 `env/ss-compare-env-alice.sh`:
```bash
"runtime.component.parameter.compare_column_idx=2"
```

修改 `env/ss-compare-env-bob.sh`:
```bash
"runtime.component.parameter.compare_column_idx=2"
```

注意：双方都设置为相同的全局列索引，算法会在握手时协商并验证。

### 使用不同的比较操作

修改 `compare_op` 参数：
- `0`: 大于
- `1`: 小于
- `2`: 等于
- `3`: 大于等于
- `4`: 小于等于

## 日志查看

日志文件位置：
- Alice: `logs/ss-compare/alice/ic_main-<timestamp>.log`
- Bob: `logs/ss-compare/bob/ic_main-<timestamp>.log`

查看最新日志：
```bash
tail -f logs/ss-compare/alice/latest.log
tail -f logs/ss-compare/bob/latest.log
```

## 性能说明

- **计算时间**：取决于样本数和特征数，通常在几秒到几十秒之间
- **通信开销**：秘密分享需要多方通信，网络延迟会影响性能
- **内存使用**：与数据规模成正比

## 下一步

- 尝试不同的列索引
- 尝试不同的比较操作
- 使用自定义数据文件
- 调整协议参数（field size, fxp_bits 等）

