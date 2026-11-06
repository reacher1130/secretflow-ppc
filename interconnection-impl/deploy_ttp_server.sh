#!/bin/bash
#
# Copyright 2023 Ant Group Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

set -e

GREEN='\033[0;32m'
NC='\033[0m'

TTP_SERVER_CONTAINER_NAME="ttp-server"
TTP_IMAGE="secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/ttp-server"
NETWORK_NAME="kuscia-exchange"

usage="$(basename "$0") [OPTIONS]

OPTIONS:
    -h       show this help text
    -i       (optional) ttp server image info, default value: secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/ttp-server

example:
 ./bfia/deploy_ttp_server.sh -i secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/ttp-server
"

while getopts ':hi:' option; do
  case "$option" in
  h)
    echo "$usage"
    exit
    ;;
  i)
    TTP_IMAGE=$OPTARG
    ;;
  \?)
    echo -e "${GREEN}Invalid option: -$OPTARG${NC}" && echo "${usage}"
    exit 1
    ;;
  esac
done

function check_ttp_server_image() {
  local has_image=false
  if docker image inspect ${TTP_IMAGE} >/dev/null 2>&1; then
    has_image=true
  fi

  if [ "$has_image" == true ] ; then
    echo -e "${GREEN}Found the ttp server image '${TTP_IMAGE}' on host${NC}"
  else
    echo -e "${GREEN}Not found the ttp server image '${TTP_IMAGE}' on host and pulling...${NC}"
    docker pull ${TTP_IMAGE}
  fi
}

function run_ttp_server() {
  echo -e "${GREEN}TTP server image: ${TTP_IMAGE}${NC}"
  check_ttp_server_image

  container_id=$(docker ps | grep ttp-server | awk '{print $1}')
  if [ "${container_id}" != "" ];then
    echo -e "${GREEN}TTP server container '${TTP_SERVER_CONTAINER_NAME}' already exists, skip deploying it${NC}"
    return
  fi

  container_id=$(docker ps -a | grep ttp-server | awk '{print $1}')
  if [ "${container_id}" != "" ];then
    docker rm ${TTP_SERVER_CONTAINER_NAME}
    return
  fi

  docker run -it -d  -p 9449:9449 --name ${TTP_SERVER_CONTAINER_NAME} --restart=always --network=${NETWORK_NAME} ${TTP_IMAGE} || exit 1
  echo -e "${GREEN}Finish deploying ttp server container '${TTP_SERVER_CONTAINER_NAME}'${NC}, and start it"

  umask 077
  KEY=$(docker exec $TTP_SERVER_CONTAINER_NAME ./beaver_server_main -gen_key=true)

  PUBLIC_KEY=$(echo "$KEY" | grep -A 1 'base64ed public key:' | tail -n 1 | sed 's/^[[:space:]]*//')

  PRIVATE_KEY=$(echo "$KEY" | grep -A 1 'base64ed private key:' | tail -n 1 | sed 's/^[[:space:]]*//')

  echo  -e "${GREEN}Generate ttp server public key: ${PUBLIC_KEY}${NC}"

  echo "$PRIVATE_KEY" > private_key.pem
  echo "$PUBLIC_KEY"  > public_key.pem
  
  docker exec -d "$TTP_SERVER_CONTAINER_NAME" ./beaver_server_main -port=9449 -server_private_key="$PRIVATE_KEY"
  echo -e "${GREEN}TTP server started${NC}"

  echo "Service started on port 9449 with private key."

  echo "Public key saved to public_key.pem"

}

run_ttp_server


