# SS-Compare 算法配置总结

## 数据文件结构

### perfect_logit_a.csv (Alice)
- 11列：x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, y
- 全局列索引：0-10
- 其中 y 是标签列（第11列，索引10）

### perfect_logit_b.csv (Bob)
- 10列：x11, x12, x13, x14, x15, x16, x17, x18, x19, x20
- 全局列索引：11-20

## 环境变量配置

### 正确的配置值

```bash
# feature_nums: 必须匹配实际数据文件的列数
'runtime.component.parameter.feature_nums={"host.0":11, "guest.0":10}'

# compare_column_idx: 全局列索引
# 0-10: Alice的列（x1-x10, y）
# 11-20: Bob的列（x11-x20）
"runtime.component.parameter.compare_column_idx=0"  # 比较Alice的第一列（x1）

# compare_op: 比较操作类型
# 0=大于, 1=小于, 2=等于, 3=大于等于, 4=小于等于
"runtime.component.parameter.compare_op=0"  # 大于
```

## 列索引说明

### 全局列索引映射

| 全局索引 | 所属方 | 列名 | 说明 |
|---------|--------|------|------|
| 0 | Alice | x1 | Alice第1列 |
| 1 | Alice | x2 | Alice第2列 |
| ... | ... | ... | ... |
| 9 | Alice | x10 | Alice第10列 |
| 10 | Alice | y | Alice标签列 |
| 11 | Bob | x11 | Bob第1列 |
| 12 | Bob | x12 | Bob第2列 |
| ... | ... | ... | ... |
| 20 | Bob | x20 | Bob第10列 |

### 示例配置

#### 示例1: 比较Alice的x1列是否大于均值
```bash
compare_column_idx=0
compare_op=0  # 大于
```

#### 示例2: 比较Bob的x11列是否小于均值
```bash
compare_column_idx=11
compare_op=1  # 小于
```

#### 示例3: 比较Alice的x5列是否等于均值
```bash
compare_column_idx=4
compare_op=2  # 等于
```

## 输出格式

### 输出文件
- Alice: `output/result-ss-compare-alice.csv`
- Bob: `output/result-ss-compare-bob.csv`

### 输出内容
- 每行一个布尔值：`true` 或 `false`
- 表示该样本的指定列是否满足比较条件
- 双方输出相同的结果

### 输出示例
```
true
false
true
true
false
...
```

## 测试步骤

### 1. 检查数据文件
```bash
# 检查Alice数据
head -1 data/perfect_logit_a.csv
# 输出: x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,y

# 检查Bob数据
head -1 data/perfect_logit_b.csv
# 输出: x11,x12,x13,x14,x15,x16,x17,x18,x19,x20
```

### 2. 检查环境脚本
```bash
# 检查Alice环境脚本
cat env/ss-compare-env-alice.sh | grep feature_nums
# 应该输出: 'runtime.component.parameter.feature_nums={"host.0":11, "guest.0":10}'

# 检查Bob环境脚本
cat env/ss-compare-env-bob.sh | grep feature_nums
# 应该输出: 'runtime.component.parameter.feature_nums={"host.0":11, "guest.0":10}'
```

### 3. 运行测试
```bash
# 使用测试脚本
bash test_ss_compare.sh

# 或手动运行
# 终端1
bash run_ic.sh ss-compare alice

# 终端2
bash run_ic.sh ss-compare bob
```

### 4. 检查结果
```bash
# 检查输出文件
ls -lh output/result-ss-compare-*.csv

# 查看结果内容
head -20 output/result-ss-compare-alice.csv
head -20 output/result-ss-compare-bob.csv

# 验证格式（应该只有true和false）
grep -vE "^(true|false)$" output/result-ss-compare-alice.csv
# 如果没有输出，说明格式正确
```

## 常见问题

### Q1: 列索引越界错误
**原因**: `compare_column_idx` 超出了有效范围
**解决**: 
- 检查 `compare_column_idx` 是否在 0-20 范围内
- 验证 `feature_nums` 是否正确

### Q2: 样本数量不一致
**原因**: 双方数据文件的样本数量不同
**解决**: 
- 确保双方数据文件的行数相同（不包括header）
- 检查数据文件格式

### Q3: 输出文件为空
**原因**: 算法执行失败
**解决**: 
- 检查日志文件: `logs/test/alice-*.log`, `logs/test/bob-*.log`
- 查看错误信息

### Q4: 结果全为true或false
**原因**: 阈值计算或比较逻辑问题
**解决**: 
- 检查比较操作类型是否正确
- 验证数据是否正常加载
- 检查SPU上下文是否正确初始化

## 算法逻辑说明

1. **数据加载**: 各方加载本地CSV文件（包括所有列）
2. **数据编码**: 将数据编码为秘密分享格式
3. **数据拼接**: 将各方数据横向拼接（Alice的11列 + Bob的10列 = 21列）
4. **列选择**: 根据 `compare_column_idx` 选择要比较的列（全局列索引）
5. **阈值计算**: 计算该列所有样本的均值作为阈值
6. **安全比较**: 在SPU上执行安全比较操作（每行的值 vs 阈值）
7. **结果输出**: 将比较结果reveal并输出为true/false

## 注意事项

1. **列索引**: `compare_column_idx` 是全局列索引，不是本地列索引
2. **特征数量**: `feature_nums` 必须匹配实际数据文件的列数（包括标签列）
3. **比较阈值**: 当前实现使用列均值作为阈值，可以修改为其他阈值
4. **输出结果**: 双方输出相同的比较结果（因为是reveal后的结果）

