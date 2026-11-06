# 修复 SS-Compare 算法错误指南

## 错误信息

```
[2025-11-10 13:50:42.297] [error] [party.cc:86] Create algo handler failed
[2025-11-10 13:50:43.298] [error] [ic_main.cc:52] run failed: [Enforce fail at ic_impl/party.cc:35] factory.
```

## 问题分析

这个错误表示 `CreateHandlerFactory()` 返回了 `nullptr`，通常是因为：

1. **Protobuf 未编译**: `ALGO_TYPE_SS_COMPARE` 枚举值不存在
2. **项目未重新编译**: 可执行文件不包含新代码
3. **算法名称转换失败**: 环境变量中的算法名称无法正确转换

## 解决步骤

### 步骤1: 检查 Protobuf 编译状态

```bash
cd /root/project/secretflow/interconnection
grep -r "ALGO_TYPE_SS_COMPARE" bazel-bin 2>/dev/null | head -1
```

如果找到，说明 protobuf 已编译。如果找不到，需要编译：

```bash
cd /root/project/secretflow/interconnection
bazel build //interconnection:ic_py_proto
```

### 步骤2: 更新 BUILD.bazel

确保 `interconnection/interconnection/handshake/algos/BUILD.bazel` 包含 `ss_compare`:

```bazel
proto_library(
    name = "algos",
    deps = [
        ":lr",
        ":psi",
        ":ss_xgb",
        ":ss_pearsonr",
        ":ss_vif",
        ":he_woe",
        ":ss_compare",  # 添加这一行
    ],
)

py_library(
    name = "algos_py_proto",
    data = [":create_init_file"],
    deps = [
        ":lr_py_proto",
        ":psi_py_proto",
        ":sgb_py_proto",
        ":ss_xgb_py_proto",
        ":ss_pearsonr_py_proto",
        ":ss_vif_py_proto",
        ":he_woe_py_proto",
        ":ss_compare_py_proto",  # 添加这一行
    ],
)

# 添加 ss_compare proto 定义
proto_library(
    name = "ss_compare",
    srcs = ["ss_compare.proto"],
)

py_proto_library(
    name = "ss_compare_py_proto",
    deps = [":ss_compare"],
)
```

### 步骤3: 重新编译 Protobuf

```bash
cd /root/project/secretflow/interconnection
bazel clean
bazel build //interconnection:ic_py_proto
```

### 步骤4: 重新编译项目

```bash
cd /root/project/secretflow/interconnection-impl
bazel clean
bazel build -c opt ic_impl/ic_main
```

### 步骤5: 验证编译结果

```bash
# 检查可执行文件
ls -lh bazel-bin/ic_impl/ic_main

# 检查是否包含 SS_COMPARE
strings bazel-bin/ic_impl/ic_main | grep -i "ss_compare\|SS_COMPARE" | head -5

# 检查 protobuf 是否包含 SS_COMPARE
grep -r "ALGO_TYPE_SS_COMPARE" bazel-bin 2>/dev/null | head -3
```

### 步骤6: 运行测试

```bash
cd /root/project/secretflow/interconnection-impl/ic_impl

# 终端1: Alice
./run_ic.sh ss-compare alice

# 终端2: Bob（等待终端1启动后）
./run_ic.sh ss-compare bob
```

## 验证清单

- [ ] Protobuf 已编译，包含 `ALGO_TYPE_SS_COMPARE`
- [ ] BUILD.bazel 已更新，包含 `ss_compare` 规则
- [ ] 项目已重新编译，可执行文件是最新的
- [ ] 环境变量中的算法名称是 `ss_compare`
- [ ] 输入数据文件存在
- [ ] 输出目录可写

## 常见问题

### Q1: 编译后仍然报错

**A**: 检查是否正确清理了旧文件：
```bash
bazel clean
bazel build -c opt ic_impl/ic_main
```

### Q2: 找不到 ALGO_TYPE_SS_COMPARE

**A**: 确保 `entry.proto` 中包含：
```protobuf
enum AlgoType {
  ...
  ALGO_TYPE_SS_COMPARE = 8;
}
```

### Q3: 算法名称转换失败

**A**: 检查环境变量中的算法名称：
- 正确: `runtime.component.parameter.algo=ss_compare`
- 错误: `runtime.component.parameter.algo=SS_COMPARE` 或 `ss-compare`

`GetFlagValue` 函数会将 `ss_compare` 转换为 `ALGO_TYPE_SS_COMPARE`。

## 调试技巧

### 1. 检查算法类型

在 `context.cc` 中添加日志：
```cpp
int32_t SuggestedAlgo() {
  auto algo_str = util::GetParamEnv("algo", FLAGS_algo);
  SPDLOG_INFO("Suggested algo string: {}", algo_str);
  auto algo = util::GetFlagValue(
      org::interconnection::v2::AlgoType_descriptor(), "ALGO_TYPE_",
      algo_str);
  SPDLOG_INFO("Suggested algo value: {}", algo);
  return algo;
}
```

### 2. 检查工厂创建

在 `party.cc` 中添加日志：
```cpp
std::unique_ptr<AlgoHandlerFactory> Party::CreateHandlerFactory() {
  SPDLOG_INFO("Current algo type: {}", ctx_->algo);
  if (ctx_->algo == org::interconnection::v2::ALGO_TYPE_SS_COMPARE) {
    SPDLOG_INFO("Creating SS-Compare handler factory");
    ...
  }
  ...
}
```

### 3. 检查环境变量

```bash
# 检查环境变量是否正确设置
source env/ss-compare-env-alice.sh
env | grep -i "algo\|protocol"
```

## 完整修复流程

```bash
# 1. 清理
cd /root/project/secretflow/interconnection-impl
bazel clean

# 2. 编译 protobuf
cd ../interconnection
bazel clean
bazel build //interconnection:ic_py_proto

# 3. 编译项目
cd ../interconnection-impl
bazel build -c opt ic_impl/ic_main

# 4. 验证
strings bazel-bin/ic_impl/ic_main | grep -i "ss_compare" | head -3

# 5. 测试
cd ic_impl
./run_ic.sh ss-compare alice  # 终端1
./run_ic.sh ss-compare bob    # 终端2
```

