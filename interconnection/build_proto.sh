#!/bin/bash

# 编译interconnection proto文件的脚本
echo "正在编译interconnection proto文件..."

# 清理旧的构建文件
bazel clean

# 编译proto包
bazel build //interconnection:ic_py_proto

if [ $? -eq 0 ]; then
    echo "✅ Proto编译成功！"
    echo "可以使用以下方式导入:"
    echo "  from interconnection.handshake.algos import ss_xgb_pb2, he_woe_pb2"
    echo "  from interconnection.handshake import entry_pb2"
    echo "  from interconnection.handshake.protocol_family import ss_pb2"
else
    echo "❌ Proto编译失败"
    exit 1
fi 