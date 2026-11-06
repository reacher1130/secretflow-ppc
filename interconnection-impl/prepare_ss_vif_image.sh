#!/bin/bash
#
# 将 ss-vif 算法镜像导入 kuscia 容器并注册 AppImage
#

set -e

GREEN='\033[0;32m'
NC='\033[0m'

KUSCIA_CONTAINER_NAME="${USER}-kuscia-autonomy-alice"
OPERATOR_IMAGE="secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/ss-vif"

usage="$(basename "$0") [OPTIONS]

OPTIONS:
    -h    show this help text
    -i    (optional) operator image info, default: ${OPERATOR_IMAGE}
    -k    (optional) kuscia container name, default: ${KUSCIA_CONTAINER_NAME}
"

while getopts ':hi:k:' option; do
  case "$option" in
  h) echo "$usage"; exit ;;
  i) OPERATOR_IMAGE=$OPTARG ;;
  k) KUSCIA_CONTAINER_NAME=$OPTARG ;;
  \?) echo -e "${GREEN}Invalid option: -$OPTARG${NC}" && echo "${usage}"; exit 1 ;;
  esac
done

echo -e "${GREEN}Operator image: ${OPERATOR_IMAGE}${NC}"
echo -e "${GREEN}Kuscia container name: ${KUSCIA_CONTAINER_NAME}${NC}"

CTR_ROOT=/home/kuscia

function prepare_operator_image() {
  local image_name=$1
  local image_tag=$2

  if [ "${OPERATOR_IMAGE}" == "" ]; then
    echo -e "${GREEN}Invalid operator image '${OPERATOR_IMAGE}'${NC}"
    exit 1
  fi

  if docker exec -it ${KUSCIA_CONTAINER_NAME} crictl inspecti ${OPERATOR_IMAGE} > /dev/null 2>&1; then
    echo -e "${GREEN}Operator image '${OPERATOR_IMAGE}' already exists in container '${KUSCIA_CONTAINER_NAME}'${NC}"
    return
  fi

  if ! docker image inspect ${OPERATOR_IMAGE} >/dev/null 2>&1; then
    echo -e "${GREEN}Not found the operator image '${OPERATOR_IMAGE}' on host and pulling...${NC}"
    docker pull ${OPERATOR_IMAGE}
  fi

  echo -e "${GREEN}Start importing operator image '${OPERATOR_IMAGE}'...${NC}"
  local image_tar=/tmp/$(echo ${image_name} | sed 's/\//_/g').tar
  docker save ${OPERATOR_IMAGE} -o ${image_tar}
  docker exec -it ${KUSCIA_CONTAINER_NAME} ctr -a=${CTR_ROOT}/containerd/run/containerd.sock -n=k8s.io images import ${image_tar}
  rm -rf ${image_tar}
  echo -e "${GREEN}Finish preparing operator image '${OPERATOR_IMAGE}' to container '${KUSCIA_CONTAINER_NAME}'${NC}"
}

function prepare_kuscia_app_image() {
  local image_name=$1
  local image_tag=$2
  app_image_name="/tmp/ss-vif-appImage.yaml"
  local image_id=$(docker inspect --format='{{.Id}}' "$image_name:$image_tag" 2>/dev/null)

echo "
apiVersion: kuscia.secretflow/v1alpha1
kind: AppImage
metadata:
  name: ss-vif
spec:
  deployTemplates:
  - name: ss-vif
    replicas: 1
    spec:
      containers:
      - name: ss-vif
        workingDir: /app
  image:
    id: ${image_id}
    name: ${image_name}
    sign: ${image_id}
    tag: ${image_tag}
" > ${app_image_name}

docker exec -it ${KUSCIA_CONTAINER_NAME} kubectl apply -f ${app_image_name}
docker exec -it ${KUSCIA_CONTAINER_NAME} kubectl annotate appimage ss-vif kuscia.secretflow/component-spec='{"component.1.description":"ss-vif","component.1.input.1.categories":"dataset","component.1.input.1.description":"train data","component.1.input.1.name":"train_data","component.1.name":"ss_vif","component.1.output.1.categories":"dataset","component.1.output.1.description":"train data","component.1.output.1.name":"train_data"}' --overwrite

rm -rf ${app_image_name}
echo -e "${GREEN}Finished preparing appImage '${OPERATOR_IMAGE}' in container '${KUSCIA_CONTAINER_NAME}'${NC}"
}

operator_image_name=$(echo "${OPERATOR_IMAGE}" | awk -F ":" '{print $1}')
operator_image_tag=$(echo "${OPERATOR_IMAGE}" | awk -F ":" '{print $2}')
if [ "${operator_image_tag}" = "" ]; then
  operator_image_tag="latest"
fi

prepare_operator_image $operator_image_name $operator_image_tag
prepare_kuscia_app_image $operator_image_name $operator_image_tag
