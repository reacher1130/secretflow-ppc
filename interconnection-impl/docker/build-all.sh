#!/bin/bash
#
# 一键构建所有 BFIA 互联互通算法镜像（C++ slim 运行时）。
# 仅包含 ic_main、libgomp 和 curl 回调客户端，不再在每个算子镜像中
# 携带 Python/requests。相比旧版 morse-ecdh-header 基座（约 1.82GB）
# 和 Python slim 版本（约 265MB）进一步缩小。
#
# 构建前: bazel build -c opt ic_impl/ic_main
# 构建后: ./prepare_all_images.sh --build  或  secretflow-allinone-package/build_bfia_algos.sh
#

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DOCKER_PLATFORM="${DOCKER_PLATFORM:-linux/amd64}"

REGISTRY="secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow"
ALL_ALGOS=("ss-lr" "ss-compare" "ss-pearson" "ss-vif" "ss-xgb")
CORE_ALGOS=("ss-lr")
ALGOS=("${ALL_ALGOS[@]}")
IC_MAIN_MIN_BYTES=$((50 * 1024 * 1024))

while [[ $# -gt 0 ]]; do
  case "$1" in
    --core-only) ALGOS=("${CORE_ALGOS[@]}"); shift ;;
    -h|--help)
      echo "用法: $(basename "$0") [--core-only]"
      echo "  --core-only  仅构建 ss-lr 镜像"
      exit 0
      ;;
    *) echo "未知参数: $1" >&2; exit 1 ;;
  esac
done

echo -e "${GREEN}=== 构建 SS 镜像（${DOCKER_PLATFORM}，算法: ${ALGOS[*]}，精简版） ===${NC}"

# 仅使用本地编译的 ic_main，不再从 morse-ecdh-header 提取旧版
IC_MAIN_BIN="${PROJECT_DIR}/bazel-bin/ic_impl/ic_main"
if [[ ! -f "${IC_MAIN_BIN}" ]]; then
    echo -e "${YELLOW}本地 ic_main 不存在，执行 bazel build...${NC}"
    cd "${PROJECT_DIR}" && bazel build -c opt ic_impl/ic_main
fi
if ! file "${IC_MAIN_BIN}" | grep -qE 'x86-64|Intel 80386'; then
    echo -e "${YELLOW}错误: ic_main 不是 linux/amd64${NC}" >&2
    file "${IC_MAIN_BIN}" >&2
    exit 1
fi
IC_MAIN_SIZE=$(stat -f%z "${IC_MAIN_BIN}" 2>/dev/null || stat -c%s "${IC_MAIN_BIN}")
echo -e "${GREEN}使用本地 ic_main (${IC_MAIN_SIZE} bytes)${NC}"
file "${IC_MAIN_BIN}"

for algo in "${ALGOS[@]}"; do
    echo -e "${GREEN}--- 构建镜像: ${algo} ---${NC}"
    BUILD_DIR="${PROJECT_DIR}/build-${algo}"
    rm -rf "${BUILD_DIR}"
    mkdir -p "${BUILD_DIR}/docker-conf"
    cp "${IC_MAIN_BIN}" "${BUILD_DIR}/"
    cp "${SCRIPT_DIR}/docker-conf/entrypoint-slim.sh" "${BUILD_DIR}/docker-conf/"
    cp "${SCRIPT_DIR}/Dockerfile.slim" "${BUILD_DIR}/Dockerfile"
    cd "${BUILD_DIR}" && docker build --platform "${DOCKER_PLATFORM}" -f Dockerfile -t "${REGISTRY}/${algo}:latest" .
    echo -e "${GREEN}构建完成: ${REGISTRY}/${algo}:latest${NC}"
    cd "${PROJECT_DIR}" && rm -rf "${BUILD_DIR}"
done

# ECDH-PSI 与 SS 算子共用同一份 ic_main C++ slim 运行时；通过别名生成
# 两个可被 AppImage 直接引用的镜像名，避免新环境再次回退到 morse 基座。
if [[ "${#ALGOS[@]}" -gt 0 ]] && docker image inspect "${REGISTRY}/${ALGOS[0]}:latest" >/dev/null 2>&1; then
    docker tag "${REGISTRY}/${ALGOS[0]}:latest" "${REGISTRY}/ecdh:latest"
    docker tag "${REGISTRY}/${ALGOS[0]}:latest" "${REGISTRY}/ecdh-psi:latest"
    echo -e "${GREEN}ECDH slim 镜像已由 ${ALGOS[0]} 运行时生成: ecdh:latest / ecdh-psi:latest${NC}"
fi

echo -e "${GREEN}=== 所有镜像构建完成（linux/amd64，请执行 prepare_all_images.sh 导入 kuscia）===${NC}"
docker images | grep -E "ecdh(-psi)?|ss-lr|ss-compare|ss-pearson|ss-vif|ss-xgb" | grep latest
echo -e "${YELLOW}下一步: cd $(dirname "$0")/.. && ./prepare_all_images.sh -a <alice> -b <bob>${NC}"
