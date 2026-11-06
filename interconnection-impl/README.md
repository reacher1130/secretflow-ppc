# 隐私计算平台互联互通协议参考实现

本仓库存放隐私计算平台互联互通协议参考实现的代码

## 构建

interconnection-impl 引用了 spu 仓库代码，需要根据[ spu 构建前提](https://github.com/secretflow/spu/blob/main/CONTRIBUTING.md#build)在编译环境上安装好依赖库

然后执行以下构建指令：

```shell
bazel build -c opt ic_impl/ic_main
```

## 快速运行脚本

为了方便使用，项目提供了 `run_ic.sh` 脚本来自动化运行各种算法。

### 脚本用法

```shell
./run_ic.sh <algo> [role]
```

参数说明：
- `<algo>`: 算法类型，支持 `ecdh-psi`、`ss-lr`、`ss-xgb`、`ss-pearson`、`ss-vif`、`ss-compare`
- `[role]`: 可选，指定角色 `alice` 或 `bob`；不提供则自动识别

### 自动角色识别

脚本支持自动识别当前机器应该运行的角色：
1. 根据本机IP与env文件中的`system.transport`IP匹配
2. 根据主机名包含`alice`或`bob`关键字判断
3. 如果无法自动识别，会提示用户显式指定角色

### 使用示例

```shell
# 自动识别角色运行 ECDH-PSI
./run_ic.sh ecdh-psi

# 指定角色运行 SS-LR
./run_ic.sh ss-lr alice

# 运行 SS-XGB（自动识别）
./run_ic.sh ss-xgb

# 运行 SS-Pearson
./run_ic.sh ss-pearson bob

# 运行 SS-Compare（安全比较）
./run_ic.sh ss-compare
```

### 脚本功能

- **自动构建检测**: 自动查找已构建的`ic_main`二进制文件
- **环境变量解析**: 自动解析env脚本中的配置参数
- **日志管理**: 自动创建带时间戳的日志文件，并创建最新日志的软链接
- **角色识别**: 支持基于IP和主机名的自动角色识别
- **错误处理**: 提供详细的错误信息和解决建议

### 日志文件

脚本运行后会在以下位置生成日志：
- 详细日志: `logs/<algo>/<role>/ic_main-<timestamp>.log`
- 最新日志软链接: `logs/<algo>/<role>/latest.log`
- 全局最新日志: `logs/latest-<algo>-<role>.log`

## 运行 ECDH-PSI

### 命令行传参

本地同时执行以下两条指令：

```shell
bazel run -c opt ic_impl/ic_main -- -rank=0 -algo=ECDH_PSI -protocol_families=ECC \
        -in_path ic_impl/data/psi_1.csv -field_names id -out_path /tmp/p1.out \
        -parties=127.0.0.1:9530,127.0.0.1:9531
```

```shell
bazel run -c opt ic_impl/ic_main -- -rank=1 -algo=ECDH_PSI -protocol_families=ECC \
        -in_path ic_impl/data/psi_2.csv -field_names id -out_path /tmp/p2.out \
        -parties=127.0.0.1:9530,127.0.0.1:9531
```

### 环境变量传参

为满足北京金融科技产业联盟的调度层互联互通标准对算法组件接口的要求，interconnection-impl 支持 ECDH-PSI 算法从环境变量读取配置参数

当某个参数在环境变量和命令行选项都被指定时，优先选择读取环境变量参数

程序运行需要关闭握手过程：
```shell
bazel run -c opt ic_impl/ic_main -- -disable_handshake=1
./bazel-bin/ic_impl/ic_main -disable_handshake=1
```

ECDH-PSI 算法配置的环境变量如下表所示。环境变量设置可参考 [ecdh-psi-env-alice.sh](./ic_impl/env/ecdh-psi-env-alice.sh) 和 [ecdh-psi-env-bob.sh](./ic_impl/env/ecdh-psi-env-bob.sh)

| 环境变量                                                   |                   参考值                    |                          描述                          |
|:-------------------------------------------------------|:----------------------------------------:|:----------------------------------------------------:|
| runtime.component.parameter.algo                       |                 ecdh_psi                 |                      algorithm                       |
| runtime.component.parameter.protocol_families          |                   ecc                    |      comma-separated list of protocol families       |
| runtime.component.parameter.curve_type                 |                curve25519                |                 elliptic curve type                  |
| runtime.component.parameter.hash_type                  |                 sha_256                  |                      hash type                       |
| runtime.component.parameter.hash2curve_strategy        |          direct_hash_as_point_x          |                hash to curve strategy                |
| runtime.component.parameter.point_octet_format         |               uncompressed               |              point Octet-String format               |
| runtime.component.parameter.bit_length_after_truncated |                    -1                    | optimization method: secondary ciphertext truncation |
| system.storage.host.url                                |           file://path/to/root            |            root path of input/output file            |
| runtime.component.input.train_data                     | {"namespace":"data","name":"psi_1.csv"}  |         relative path and name of input file         |
| runtime.component.parameter.field_names                |                    id                    |                     field names                      |
| runtime.component.parameter.result_to_rank             |                    -1                    |              which rank gets the result              |
| runtime.component.output.train_data                    | {"namespace":"output","name":"result_a"} |        relative path and name of output file         |

## 运行 SS-LR

### 使用脚本运行（推荐）

```shell
# 自动识别角色
./run_ic.sh ss-lr

# 或指定角色
./run_ic.sh ss-lr alice
./run_ic.sh ss-lr bob
```

### 启动 Beaver 服务

运行 SS-LR 之前，需要先启动 Beaver 服务。Beaver 服务的代码位于 SPU 仓库中，需要将 SPU 代码克隆到本地并编译：

```shell
git clone git@github.com:secretflow/spu.git
cd spu && bazel build -c opt libspu/mpc/semi2k/beaver/beaver_impl/ttp_server:beaver_server_main
```

在当前机器上生成独立的 Beaver 密钥，不复用仓库或其他环境的密钥。私钥只保存在本机受限文件中：

```shell
umask 077
BEAVER_BIN=bazel-bin/libspu/mpc/semi2k/beaver/beaver_impl/ttp_server/beaver_server_main
"$BEAVER_BIN" -gen_key=true > beaver-keys.local.txt
awk '/base64ed private key:/{getline; sub(/^[[:space:]]*/, ""); print; exit}' beaver-keys.local.txt > private_key.pem
awk '/base64ed public key:/{getline; sub(/^[[:space:]]*/, ""); print; exit}' beaver-keys.local.txt > public_key.pem
test -s private_key.pem && test -s public_key.pem
PRIVATE_KEY="$(cat private_key.pem)"
PUBLIC_KEY="$(cat public_key.pem)"
rm -f beaver-keys.local.txt
"$BEAVER_BIN" -port=9449 -server_private_key="$PRIVATE_KEY"
```

保持 `private_key.pem` 的权限为 `0600`，不要上传或提交密钥。后面的两个参与方使用本次生成的同一 `PUBLIC_KEY`；只向参与方提供公钥。

### 命令行传参

启动 Beaver 服务后，本地同时执行以下两条指令，命令行参数包括上述 Beaver 服务生成的公钥：

```shell
bazel run -c opt ic_impl/ic_main -- -rank=0 -algo=SS_LR -protocol_families=SS \
        -dataset=ic_impl/data/perfect_logit_a.csv -has_label=true \
        -use_ttp=true -ttp_server_host=127.0.0.1:9449 \
        -parties=127.0.0.1:9530,127.0.0.1:9531 -ttp_asym_crypto_schema=sm2 \
        -ttp_public_key="$PUBLIC_KEY"
```

```shell
bazel run -c opt ic_impl/ic_main -- -rank=1 -algo=SS_LR -protocol_families=SS \
        -dataset=ic_impl/data/perfect_logit_b.csv -has_label=false \
        -use_ttp=true -ttp_server_host=127.0.0.1:9449 \
        -parties=127.0.0.1:9530,127.0.0.1:9531 -ttp_asym_crypto_schema=sm2 \
        -ttp_public_key="$PUBLIC_KEY"
```

### 环境变量传参

为满足北京金融科技产业联盟的调度层互联互通标准对算法组件接口的要求，interconnection-impl 支持 SS-LR 算法从环境变量读取配置参数

当某个参数在环境变量和命令行选项都被指定时，优先选择读取环境变量参数

程序运行需要关闭握手过程：
```shell
bazel run -c opt ic_impl/ic_main -- -disable_handshake=1
./bazel-bin/ic_impl/ic_main -disable_handshake=1
```

SS-LR 算法配置的环境变量如下表所示。环境变量设置可参考 [ss-lr-env-alice.sh](./ic_impl/env/ss-lr-env-alice.sh) 和 [ss-lr-env-bob.sh](./ic_impl/env/ss-lr-env-bob.sh)

| 环境变量                                               |                        参考值                        |                            描述                             |
|:---------------------------------------------------|:-------------------------------------------------:|:---------------------------------------------------------:|
| runtime.component.parameter.algo                   |                       ss_lr                       |                         algorithm                         |
| runtime.component.parameter.protocol_families      |                        ss                         |         comma-separated list of protocol families         |
| runtime.component.parameter.batch_size             |                        21                         |                    size of each batch                     |
| runtime.component.parameter.last_batch_policy      |                      discard                      |  policy to process the partial last batch of each epoch   |
| runtime.component.parameter.num_epoch              |                         1                         |                      number of epoch                      |
| runtime.component.parameter.l0_norm                |                         0                         |                      l0 penalty term                      |
| runtime.component.parameter.l1_norm                |                         0                         |                      l1 penalty term                      |
| runtime.component.parameter.l2_norm                |                        0.5                        |                      l2 penalty term                      |
| runtime.component.parameter.optimizer              |                        sgd                        |        optimization algorithm to speed up training        |
| runtime.component.parameter.learning_rate          |                      0.0001                       |         learning rate parameter in sgd optimizer          |
| runtime.component.parameter.sigmoid_mode           |                     minimax_1                     |               sigmoid approximation method                |
| runtime.component.parameter.protocol               |                      semi2k                       |                     ss protocol type                      |
| runtime.component.parameter.field                  |                        64                         |        field type, 64 for Ring64, 128 for Ring128         |
| runtime.component.parameter.fxp_bits               |                        18                         |       number of fraction bits of fixed-point number       |
| runtime.component.parameter.trunc_mode             |                   probabilistic                   |                      truncation mode                      |
| runtime.component.parameter.shard_serialize_format |                        raw                        | serialization format used for communicating secret shares |
| runtime.component.parameter.use_ttp                |                       true                        |               whether to use beaver service               |
| runtime.component.parameter.ttp_server_host        |                      ip:port                      |   remote ip:port or load-balance uri of beaver service    |
| runtime.component.parameter.ttp_asym_crypto_schema |                        sm2                        |           asym_crypto_schema of beaver service            |
| runtime.component.parameter.ttp_public_key         |                                                   |               public key of beaver service                |
| runtime.component.parameter.ttp_adjust_rank        |                         0                         |      which rank do adjust rpc call to beaver service      |
| system.storage.host.url                            |                file://path/to/root                |              root path of input/output file               |
| runtime.component.input.train_data                 | {"namespace":"data","name":"perfect_logit_a.csv"} |           relative path and name of input file            |
| runtime.component.parameter.skip_rows=1            |                         1                         |            number of skipped rows from dataset            |
| runtime.component.parameter.label_owner            |                      host.0                       |             which party owns the label column             |
| runtime.component.parameter.feature_nums           |            {"host.0":10, "guest.0":10}            |             feature column nums of each party             |
| runtime.component.output.train_data                |     {"namespace":"output","name":"result_a"}      |           relative path and name of output file           |

## 运行 SS-XGB

### 使用脚本运行（推荐）

```shell
# 自动识别角色
./run_ic.sh ss-xgb

# 或指定角色
./run_ic.sh ss-xgb alice
./run_ic.sh ss-xgb bob
```
## 运行 SS-Pearson

SS-Pearson 为纵向两方（可扩展多方）基于秘密分享的相关系数计算 Demo，依赖与 SS-LR 相同的 SS 协议与（可选）TTP Beaver 服务。

### 使用脚本运行（推荐）

```shell
# 自动识别角色
./run_ic.sh ss-pearson

# 或指定角色
./run_ic.sh ss-pearson alice
./run_ic.sh ss-pearson bob
```

### 传统运行方式

1) 启动两端：

```bash
bazel run -c opt ic_impl/ic_main -- -rank=0 -algo=SS_PEARSONR -protocol_families=SS
bazel run -c opt ic_impl/ic_main -- -rank=1 -algo=SS_PEARSONR -protocol_families=SS
```

或使用环境脚本：

```bash
ic_impl/env/ss-pearson-env-alice.sh
ic_impl/env/ss-pearson-env-bob.sh
```

2) 数据与输出：

- ENV 参数说明（与 SS-LR/SS-XGB 对齐）：

| 键 | 示例 | 说明 |
| --- | --- | --- |
| `system.storage.host.url` | `file:///root/project/secretflow/interconnection-impl/ic_impl/` | 本地文件根目录 |
| `runtime.component.input.train_data` | `{ "namespace":"data", "name":"perfect_logit_a.csv" }` | 输入 CSV |
| `runtime.component.output.train_data` | `{ "namespace":"output", "name":"result_a.csv" }` | 结果输出 CSV |
| `runtime.component.parameter.algo` | `ss_pearson` | 算法名 |
| `runtime.component.parameter.protocol_families` | `ss` | 协议族 |
| `runtime.component.parameter.protocol` | `semi2k` | SS 协议 |
| `runtime.component.parameter.field` | `64` | 整数环位数 |
| `runtime.component.parameter.fxp_bits` | `18` | 定点小数位 |
| `runtime.component.parameter.trunc_mode` | `probabilistic` | 截断方式 |
| `runtime.component.parameter.shard_serialize_format` | `raw` | 分片序列化格式 |
| `runtime.component.parameter.use_ttp` | `false` | 是否启用 TTP |
| `runtime.component.parameter.ttp_server_host` | `ip:port` | TTP 主机 |
| `runtime.component.parameter.ttp_asym_crypto_schema` | `sm2` | TTP 非对称算法 |
| `runtime.component.parameter.ttp_public_key` | base64 | TTP 公钥（base64） |
| `runtime.component.parameter.ttp_adjust_rank` | `0` | 调整方 rank |
| `runtime.component.parameter.feature_select` | `{"host.0":["x1","x2"],"guest.0":["x3"]}` | 各方特征列名（可选，默认全列） |
| `runtime.component.parameter.feature_nums` | `{"host.0":8,"guest.0":7}` | 各方特征数（仅在禁用握手时需要） |
| `runtime.component.parameter.result_to_rank` | `-1` | 结果揭示给 `-1`（全体）或指定 rank |
| `runtime.component.parameter.skip_rows` | `1` | CSV 跳过行数（表头） |
| `runtime.component.parameter.start_transport` | `true` | 是否在本地启动传输服务 |
| `system.transport` | `ip:port` | 本端监听地址（黑盒传输） |
| `config.self_role` | `host.0` | 本方角色 |
| `config.node_ip.*` 等 | 见 ss-lr | 传输配置 |

说明：
- 默认启用握手时，`feature_nums/sample_size` 等由握手协商；当 `--disable_handshake` 为 true 时，可通过 ENV 提供。
- 相关矩阵将按照 `runtime.component.output.train_data` 写出到本地 CSV。

- 输入采用与 SS-LR/SS-XGB 相同的 CSV 读取约定：首行为逗号分隔表头。Demo 会在各方本地读取 `data.csv` 并根据 `feature_select` 选列。
- 结果为相关矩阵（当前 Demo 输出协方差矩阵作为占位，后续可替换为标准化后的 Pearson 矩阵），可按 `result_to_rank` 揭示给指定方或全体。


### 启动 Beaver 服务

SS-XGB 使用与 SS-LR 相同的 Beaver（三元组）服务，参考上文“运行 SS-LR → 启动 Beaver 服务”的步骤完成编译、生成密钥并启动服务。

### 命令行传参

在 Beaver 服务启动后，本地同时执行以下两条指令启动 SS-XGB：

```shell
bazel run -c opt ic_impl/ic_main -- -rank=0 -algo=SS_XGB -protocol_families=SS \
        -parties=127.0.0.1:9530,127.0.0.1:9531
```

```shell
bazel run -c opt ic_impl/ic_main -- -rank=1 -algo=SS_XGB -protocol_families=SS \
        -parties=127.0.0.1:9530,127.0.0.1:9531
```

### 说明

- 当前 SS-XGB Demo 默认从进程工作目录读取名为 `data.csv` 的数据文件，请在两方运行目录下准备该文件。
- `data.csv` 建议包含表头，默认标签列名为 `label`；若无该列名，默认按最后一列作为标签。
- Demo 默认由 `rank=0` 持有标签列；其他方标签内部将使用占位值，仅用于形状对齐与评估（仅标签方进行准确率评估）。
- 实际生产推荐通过 Pipeline/握手配置明确 I/O 参数（样本数、各方特征列数、标签持有方等）与 TTP 参数（服务地址、公钥等）。

### 环境变量传参

为满足北京金融科技产业联盟的调度层互联互通标准对算法组件接口的要求，interconnection-impl 支持 SS-XGB 通过环境变量配置协议/Beaver 相关参数（其解析逻辑与 SS-LR 相同）。目前 Demo 的 I/O 与超参读取以默认/握手为准，以下列出已生效与预留项：

已生效（运行时可生效）

| 环境变量                                               |           参考值           |                            描述                             |
|:---------------------------------------------------|:-------------------------:|:---------------------------------------------------------:|
| runtime.component.parameter.algo                   |           ss_xgb          |                         algorithm                         |
| runtime.component.parameter.protocol_families      |            ss             |         comma-separated list of protocol families         |
| runtime.component.parameter.protocol               |          semi2k           |                     ss protocol type                      |
| runtime.component.parameter.field                  |            64             |        field type, 64 for Ring64, 128 for Ring128         |
| runtime.component.parameter.fxp_bits               |            18             |       number of fraction bits of fixed-point number       |
| runtime.component.parameter.trunc_mode             |       probabilistic       |                      truncation mode                      |
| runtime.component.parameter.shard_serialize_format |            raw            | serialization format used for communicating secret shares |
| runtime.component.parameter.use_ttp                |           true            |               whether to use beaver service               |
| runtime.component.parameter.ttp_server_host        |          ip:port          |   remote ip:port or load-balance uri of beaver service    |
| runtime.component.parameter.ttp_asym_crypto_schema |            sm2            |           asym_crypto_schema of beaver service            |
| runtime.component.parameter.ttp_public_key         |      base64-encoded       |               public key of beaver service                |
| runtime.component.parameter.ttp_adjust_rank        |             0             |      which rank do adjust rpc call to beaver service      |

预留/建议（当前 Demo 版本可能由默认值/握手决定，环境变量暂不生效；用于与规范/管道接口对齐）

| 环境变量                                      | 参考值 |            描述             |
|:------------------------------------------|:----:|:-------------------------:|
| runtime.component.parameter.num_boost_round |  3   |        提升轮数（树数量）        |
| runtime.component.parameter.max_depth      |  4   |           树深度上限           |
| runtime.component.parameter.learning_rate  | 0.1  |           学习率 shrinkage     |
| runtime.component.parameter.reg_lambda     | 1.0  |           L2 正则             |
| runtime.component.parameter.gamma          | 0.0  |        分裂最小增益阈值          |
| runtime.component.parameter.base_score     | 0.5  |         初始分数（偏置）         |
| runtime.component.parameter.seed           |  42  |            随机种子            |
| runtime.component.parameter.objective      |  1   |  目标函数（例如 1=logistic）    |
| runtime.component.parameter.num_bins       |  16  |      分桶数（候选分裂点枚举）      |

### 数据文件示例

当前 Demo 解析规则如下：

- 第一行表头使用逗号分隔，例如 `label,f1,f2,f3`；
- 从第二行起的数据行为空格分隔的数值序列（便于快速解析），示例如下：

```text
label,f1,f2,f3
1 0.12 0.35 0.27
0 0.55 0.10 0.42
1 0.08 0.90 0.31
0 0.40 0.22 0.18
```

注意：默认 `rank=0` 为标签持有方，且若未找到列名 `label`，则按最后一列作为标签列。

## run_ic.sh 脚本详细说明

### 脚本特性

`run_ic.sh` 脚本提供了完整的自动化运行解决方案，具有以下特性：

#### 1. 智能角色识别
- **IP匹配**: 根据本机IP与env文件中的`system.transport`IP自动匹配
- **主机名识别**: 根据主机名包含`alice`或`bob`关键字判断
- **手动指定**: 支持显式指定角色，格式为 `./run_ic.sh <algo> <role>`

#### 2. 自动构建检测
脚本会自动在以下位置查找已构建的`ic_main`二进制文件：
- `$REPO_ROOT/bazel-bin/interconnection-impl/ic_impl/ic_main`
- `$IC_IMPL_DIR/../bazel-bin/ic_impl/ic_main`
- `$IC_IMPL_DIR/../../bazel-bin/ic_impl/ic_main`

#### 3. 环境变量解析
- 自动解析env脚本中的配置参数
- 支持复杂的多行环境变量设置
- 自动处理引号和转义字符

#### 4. 日志管理
- 自动创建带时间戳的日志文件
- 创建最新日志的软链接便于查看
- 支持多算法、多角色的日志隔离

#### 5. 错误处理
- 提供详细的错误信息和解决建议
- 检查依赖文件和配置的完整性
- 友好的用户提示信息

### 日志文件结构

```
logs/
├── ecdh-psi/
│   ├── alice/
│   │   ├── ic_main-20240925-103000.log
│   │   └── latest.log -> ic_main-20240925-103000.log
│   └── bob/
│       ├── ic_main-20240925-103000.log
│       └── latest.log -> ic_main-20240925-103000.log
├── ss-lr/
│   ├── alice/
│   └── bob/
└── latest-<algo>-<role>.log -> logs/<algo>/<role>/latest.log
```

### 故障排除

#### 常见问题

1. **未找到可执行文件**
   ```
   未找到可执行文件。请先构建 ic_main：
     (在 interconnection-impl 目录下)  bazel build -c opt ic_impl/ic_main
     (在仓库根目录下)            bazel build -c opt //interconnection-impl/ic_impl:ic_main
   ```
   解决：按照提示构建二进制文件

2. **无法自动识别角色**
   ```
   无法自动识别角色，请显式指定 alice 或 bob
   ```
   解决：显式指定角色，如 `./run_ic.sh ss-lr alice`

3. **缺少env文件**
   ```
   缺少 env 文件: /path/to/env-alice.sh 或 /path/to/env-bob.sh
   ```
   解决：确保env文件存在于`ic_impl/env/`目录下

4. **解析env失败**
   ```
   解析 env 失败: /path/to/env-alice.sh
   ```
   解决：检查env文件格式是否正确

## FAQ

若构建失败并提示 `Host key verification failed`，解决方式如下:

* 首先，[向github账户添加SSH key](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/adding-a-new-ssh-key-to-your-github-account)

* 然后在本地执行以下指令(以选择 RSA 作为 SSH key 类型为例)：

```shell
ssh-keyscan -t rsa github.com >> ~/.ssh/known_hosts
```