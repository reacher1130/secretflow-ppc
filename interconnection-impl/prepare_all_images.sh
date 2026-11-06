#!/bin/bash
#
# 将本地编译的 BFIA 算法镜像导入 kuscia 并注册 AppImage。
# ECDH-PSI 与 SS 五算法统一使用本地 C++ slim 运行时，避免继续绑定
# 约 1.82GB 的 morse-ecdh-header 基座。
#   cd interconnection-impl
#   bazel build -c opt ic_impl/ic_main
#   ./docker/build-all.sh
#
# 用法:
#   ./prepare_all_images.sh -a gaofeng-kuscia-autonomy-alice -b gaofeng-kuscia-autonomy-bob
#   ./prepare_all_images.sh --core-only -a ... -b ...   # 方案 C：仅 ss-lr + ECDH
#

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[31m'
NC='\033[0m'

ALICE_CONTAINER="${USER}-kuscia-autonomy-alice"
BOB_CONTAINER="${USER}-kuscia-autonomy-bob"
REGISTRY="secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow"
CTR_ROOT=/home/kuscia
DO_BUILD=false
FORCE_IMPORT=false
CORE_ONLY=false

# 本地 build/import 的五个 SS 算法；ECDH slim 镜像在 register_ecdh_appimages 中单独导入。
ALL_ALGOS=("ss-lr" "ss-compare" "ss-pearson" "ss-vif" "ss-xgb")
CORE_ALGOS=("ss-lr")
ALGOS=("${ALL_ALGOS[@]}")
ECDH_IMAGE="${REGISTRY}/ecdh:latest"
ECDH_PSI_IMAGE="${REGISTRY}/ecdh-psi:latest"

usage="$(basename "$0") [OPTIONS]

将本地编译的 BFIA 算法镜像导入 kuscia 并注册 AppImage（含 ECDH slim）。

OPTIONS:
    -h    show this help text
    -a    alice 容器名（默认: ${ALICE_CONTAINER}）
    -b    bob 容器名（默认: ${BOB_CONTAINER}）
    --build  先执行 docker/build-all.sh 构建 SS 五算法镜像
    --force  强制覆盖 kuscia 容器内已有同名镜像（本地 build 后必用）
    --core-only  方案 C：仅导入/注册 ss-lr + ECDH，不因缺少 ss-xgb 等失败

example:
 ./prepare_all_images.sh --build -a ${USER}-kuscia-autonomy-alice -b ${USER}-kuscia-autonomy-bob
"

while [[ $# -gt 0 ]]; do
  case "$1" in
  h|-h)
    echo "$usage"
    exit 0
    ;;
  a|-a)
    ALICE_CONTAINER=$2
    shift 2
    ;;
  b|-b)
    BOB_CONTAINER=$2
    shift 2
    ;;
  --build)
    DO_BUILD=true
    shift
    ;;
  --force)
    FORCE_IMPORT=true
    shift
    ;;
  --core-only)
    CORE_ONLY=true
    ALGOS=("${CORE_ALGOS[@]}")
    shift
    ;;
  *)
    echo -e "${RED}Invalid option: $1${NC}" && echo "${usage}"
    exit 1
    ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BFIA_DIR="${SCRIPT_DIR}/../../kuscia/scripts/user/bfia"

import_image_to_container() {
  local container=$1
  local image=$2

  echo -e "${GREEN}  检查容器 ${container} 中是否已有镜像 ${image}...${NC}"
  if ! $FORCE_IMPORT && docker exec "${container}" crictl inspecti "${image}" > /dev/null 2>&1; then
    echo -e "${YELLOW}  镜像已存在，跳过（加 --force 覆盖）${NC}"
    return 0
  fi

  if ! docker image inspect "${image}" > /dev/null 2>&1; then
    echo -e "${RED}  宿主机无镜像 ${image}，请先 ./docker/build-all.sh${NC}"
    return 1
  fi

  echo -e "${GREEN}  导出并导入镜像 ${image}...${NC}"
  local image_tar="/tmp/$(echo "${image}" | sed 's/[\/:]/_/g').tar"
  docker save "${image}" -o "${image_tar}"
  docker cp "${image_tar}" "${container}:${image_tar}"
  docker exec "${container}" ctr -a="${CTR_ROOT}/containerd/run/containerd.sock" -n=k8s.io images import "${image_tar}"
  docker exec "${container}" rm -f "${image_tar}"
  rm -f "${image_tar}"
}

apply_appimage() {
  local container=$1
  local yaml_file=$2
  echo -e "${GREEN}  注册 AppImage: $(basename "${yaml_file}") → ${container}${NC}"
  docker cp "${yaml_file}" "${container}:/tmp/appimage.yaml"
  docker exec "${container}" kubectl apply -f /tmp/appimage.yaml
  docker exec "${container}" rm -f /tmp/appimage.yaml
}

import_algo() {
  local algo=$1
  local image="${REGISTRY}/${algo}:latest"
  local ok=0

  import_image_to_container "${ALICE_CONTAINER}" "${image}" || ok=1
  import_image_to_container "${BOB_CONTAINER}" "${image}" || ok=1
  [[ "$ok" -eq 0 ]] || return 1
  return 0
}

register_ecdh_appimages() {
  echo -e "${GREEN}--- ECDH-PSI (C++ slim) ---${NC}"
  local ok=0
  for image in "${ECDH_IMAGE}" "${ECDH_PSI_IMAGE}"; do
    if ! docker image inspect "${image}" > /dev/null 2>&1; then
      echo -e "${RED}  宿主机未找到 ${image}，请先构建 C++ slim 镜像${NC}"
      ok=1
      continue
    fi
    import_image_to_container "${ALICE_CONTAINER}" "${image}" || ok=1
    import_image_to_container "${BOB_CONTAINER}" "${image}" || ok=1
  done
  for yaml in ic-ecdh.yaml ic-ecdh-psi.yaml; do
    [[ -f "${BFIA_DIR}/${yaml}" ]] || continue
    apply_appimage "${ALICE_CONTAINER}" "${BFIA_DIR}/${yaml}"
    apply_appimage "${BOB_CONTAINER}" "${BFIA_DIR}/${yaml}"
  done
  [[ "$ok" -eq 0 ]]
}

register_appimages_for_algo() {
  local algo=$1
  local yamls=("ic-${algo}.yaml")

  local y
  for y in "${yamls[@]}"; do
    local path="${BFIA_DIR}/${y}"
    if [[ -f "${path}" ]]; then
      apply_appimage "${ALICE_CONTAINER}" "${path}"
      apply_appimage "${BOB_CONTAINER}" "${path}"
    else
      echo -e "${YELLOW}  未找到 ${path}，跳过注册${NC}"
    fi
  done
}

if $DO_BUILD; then
  echo -e "${GREEN}=== 本地构建 SS 镜像 ===${NC}"
  if $CORE_ONLY; then
    bash "${SCRIPT_DIR}/docker/build-all.sh" --core-only
  else
    bash "${SCRIPT_DIR}/docker/build-all.sh"
  fi
fi

if [ ! -d "${BFIA_DIR}" ]; then
  echo -e "${RED}未找到 BFIA AppImage 配置目录: ${BFIA_DIR}${NC}"
  exit 1
fi

echo -e "${GREEN}=== 导入本地镜像并注册 AppImage ===${NC}"
echo -e "${GREEN}Alice: ${ALICE_CONTAINER}  Bob: ${BOB_CONTAINER}${NC}"
if $CORE_ONLY; then
  echo -e "${YELLOW}方案 C：仅 ss-lr + ECDH；ss-xgb 等四算法待全量 ic_main 后再补。${NC}"
else
  echo -e "${YELLOW}说明: SS 五算法和 ECDH 均使用本地 C++ slim 镜像；旧 morse 镜像仅为兼容历史任务保留。${NC}"
fi

MISSING=0
DONE=0

if register_ecdh_appimages; then
  DONE=$((DONE + 1))
else
  MISSING=$((MISSING + 1))
fi

for algo in "${ALGOS[@]}"; do
  echo -e "${GREEN}--- ${algo} ---${NC}"
  IMAGE="${REGISTRY}/${algo}:latest"
  if ! docker image inspect "${IMAGE}" > /dev/null 2>&1; then
    echo -e "${RED}  宿主机未找到 ${IMAGE}${NC}"
    if $CORE_ONLY; then
      echo -e "${RED}  请先: ./docker/build-all.sh --core-only${NC}"
    else
      echo -e "${RED}  请先: cd ${SCRIPT_DIR} && bazel build -c opt ic_impl/ic_main && ./docker/build-all.sh${NC}"
    fi
    MISSING=$((MISSING + 1))
    continue
  fi
  if import_algo "${algo}"; then
    register_appimages_for_algo "${algo}"
    DONE=$((DONE + 1))
    echo -e "${GREEN}  ${algo} 完成${NC}"
  else
    MISSING=$((MISSING + 1))
  fi
done

echo ""
echo -e "${GREEN}=== 汇总: ${DONE} 个算法就绪, ${MISSING} 个缺失镜像 ===${NC}"
echo -e "${GREEN}Alice AppImage:${NC}"
docker exec "${ALICE_CONTAINER}" kubectl get appimage 2>/dev/null | grep -E '^NAME|ic-' || true

if [[ "${MISSING}" -gt 0 ]]; then
  exit 1
fi
