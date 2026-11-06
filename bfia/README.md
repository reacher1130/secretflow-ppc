# BFIA 互联互通开发目录

本目录用于承载 BFIA 互联互通的开发实例。底层协议实现仍位于
`secretflow/ic`，这里提供可以分别启动的双方示例和本地双进程启动脚本。

## 当前实例

- `sgb/alice.py`：Alice，标签持有方，也是 BFIA 握手中的被动方。
- `sgb/bob.py`：Bob，特征持有方，也是 BFIA 握手中的主动方。
- `sgb/common.py`：双方共用的数据集、网络和 SGB/PHE 配置。
- `sgb/run_local.sh`：在同一台机器上启动 Alice 和 Bob。
- `he_woe/alice.py` / `he_woe/bob.py`：BFIA HE-WOE 双方实例。
- `he_woe/run_local.sh`：在同一台机器上启动 HE-WOE 双方。

该示例使用 breast cancer 数据集，Alice 使用前 15 个特征并持有标签，Bob 使用剩余特征。SGB 示例通过 BFIA SGB 握手协商参数，并使用 `ic-paillier` 保护梯度/海森矩阵相关计算；HE-WOE 示例通过 BFIA HE-WOE 握手协商分箱参数，并使用 Paillier 保护标签统计。

## 一键验证

从 `NewProject/secretflow` 目录执行：

```shell
# 只验证 Python SGB 和 HE-WOE
PYTHON_BIN=/path/to/python ./bfia/run_python_tests.sh

# 验证 C++ 参考实现的六个算法
./bfia/run_cpp_tests.sh

# 按顺序验证 Python 和 C++
./bfia/run_all_tests.sh
```

Python 脚本会启动双方进程，保存双方日志和 HE-WOE 报告；C++ 脚本会依次验证
ECDH-PSI、SS-LR、SS-XGB、SS-Pearson、SS-VIF、SS-Compare。默认结果目录为
`bfia/results/<python|cpp>/<timestamp>/`，同级的 `latest` 指向最近一次运行。

两类脚本都会在摘要和双方日志中打印 `BFIA协议`、Alice/Bob 的 IPv4 端点、监听端点、
握手/算法阶段和通信完成标记；测试脚本还会检查这些标记，避免只依据进程退出码判定
“通信成功”。Python 默认使用本机自动探测的两个 IPv4 地址（Alice 默认
`127.0.0.1`，Bob 默认取默认网卡的 IPv4），监听仍使用 `0.0.0.0:<port>`；可用
`BFIA_PYTHON_ALICE_IP` 和 `BFIA_PYTHON_BOB_IP` 覆盖。Python 日志中的
`BFIA协议实际发送/实际接收` 是对 `LinkProxy` 底层 BRPC 链路调用的计数。C++ Docker
测试固定使用两个容器 IPv4：`172.31.240.2` 和 `172.31.240.3`。

C++ 脚本的运行时选择：

```shell
# 先构建一次 C++ 专用精简运行镜像（仅 debian + libgomp1 + ic_main）
bash ./interconnection-impl/docker/build_cpp_runtime_slim.sh

# 自动选择：优先使用 C++ 精简运行镜像；如果尚未构建，则自动回退到
# ss-compare:latest 兼容镜像。当前本机 ic_main 会优先挂载到容器内。
./bfia/run_cpp_tests.sh

# 明确使用 Docker 镜像
BFIA_CPP_RUNTIME=docker ./bfia/run_cpp_tests.sh

# 明确指定精简镜像
BFIA_CPP_RUNTIME=docker \
BFIA_CPP_IMAGE=secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/bfia-cpp-runtime:linux-amd64-slim \
./bfia/run_cpp_tests.sh

# 明确使用本机可执行文件（适用于架构匹配的 Linux 构建）
BFIA_CPP_RUNTIME=binary ./bfia/run_cpp_tests.sh

# 强制使用镜像内置二进制；默认 auto 会优先使用当前本机 ic_main
BFIA_CPP_BINARY_IN_DOCKER=0 ./bfia/run_cpp_tests.sh
```

当前 macOS arm64 工作站上的 `ic_main` 是 Linux x86-64 文件，不能直接执行；
如果 Docker 中已有 amd64 BFIA 镜像，脚本会把它挂载进两个临时容器完成 C++ 双方运行。
脚本只使用小规模 `perfect_logit_*.csv` 做验证，避免把 1000 万行压力数据作为默认
冒烟测试。精简镜像只用于 C++ 双进程测试；需要 `run.py`、Python `requests` 回调和
Kuscia 运行环境的生产镜像仍使用 `Dockerfile.bfia` 或 `Dockerfile.slim`。

## 环境

项目要求 Python 3.10 或 3.11。需要安装当前 SecretFlow 项目及其依赖，并安装仓库内的 `interconnection` 包；开发目录中的入口会自动把源码路径加入 `PYTHONPATH`。

例如在仓库根目录执行：

```shell
python3 -m pip install -e interconnection
python3 -m pip install -e .
```

如果依赖已安装，也可以直接运行下面的双方实例。

## 启动

在 `bfia/sgb` 目录下分别执行：

```shell
python3 alice.py --log-level debug
python3 bob.py --log-level debug
```

或者在仓库根目录执行：

```shell
./bfia/sgb/run_local.sh
```

默认监听地址为：

- Alice：`0.0.0.0:9844`
- Bob：`0.0.0.0:9845`

跨机器运行时，可以通过 `--alice-address`、`--bob-address` 和对应的 `--*-listen` 参数修改地址。例如：

```shell
python3 alice.py --alice-address 10.0.0.1:9844 --bob-address 10.0.0.2:9845
python3 bob.py --alice-address 10.0.0.1:9844 --bob-address 10.0.0.2:9845
```

HE-WOE 的启动方式对应 `bfia/he_woe`：

```shell
python3 alice.py --binning-method quantile
python3 bob.py --binning-method quantile
```

两端的 `--binning-method`、`--bin-num` 和 `--key-size` 应保持一致；生产环境不要使用
示例默认之外的弱密钥配置。

## 与底层实现的关系

入口最终调用：

```python
from secretflow.ic.runner import run
run(config=config, dataset=dataset)
```

`runner` 根据 `algo` 创建 `SgbIcHandler` 或 `HeWoeIcHandler`。因此本目录是运行和后续开发入口，不复制底层 BFIA 协议代码。

HE-WOE 使用 `algo = "he_woe"`，由 `secretflow/ic/handler/he_woe_handler.py`
处理握手和数据阶段。Alice 持有标签和 Paillier 私钥，Bob 只接收公钥、密文标签，
并在本地对每个分箱执行密文求和。分箱边界不离开特征持有方；WOE/IV 由 Alice
计算后返回给对应特征持有方。

C++ 冒烟测试默认在本次结果目录生成固定种子的合成 CSV，因此干净 Git 检出不需要本地数据文件。可通过 `BFIA_CPP_DATA_ROOT` 指定已有测试数据目录；该目录的文件不会被生成器改写。
