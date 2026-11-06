// Copyright 2023 Ant Group Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ic_impl/party.h"

#include "spdlog/spdlog.h"

#include "ic_impl/context.h"
#include "ic_impl/factory.h"

namespace ic_impl {

Party::Party(std::shared_ptr<IcContext> ctx) : ctx_(std::move(ctx)) {}

Party::~Party() = default;

void Party::Run() {
  //
  int32_t self_rank = ctx_->lctx->Rank();
  SPDLOG_INFO("BFIA协议Party开始：本方rank={}，握手方向={}", self_rank,
              self_rank == 0 ? "被动接收" : "主动发起");
  if (ctx_->version == 1) {  // send request from rank 0 to rank 1
    YACL_THROW("handshake version 1 has been discarded");
  } else {  // send requests from rank 1 ~ n-1 to rank 0
    auto factory = CreateHandlerFactory();
    YACL_ENFORCE(factory);
    auto handler_v2 = factory->CreateAlgoV2Handler(ctx_);

    int32_t recv_rank = 0;
    if (self_rank == recv_rank) {
      handler_v2->PassiveRun();
    } else {
      handler_v2->ActiveRun(recv_rank);
    }
  }
}

std::unique_ptr<AlgoHandlerFactory> Party::CreateHandlerFactory() {
  if (ctx_->algo == org::interconnection::v2::ALGO_TYPE_ECDH_PSI) {
    YACL_ENFORCE(!ctx_->protocol_families.empty());
    YACL_ENFORCE(ctx_->protocol_families.at(0) ==
                 org::interconnection::v2::PROTOCOL_FAMILY_ECC);
    SPDLOG_INFO("BFIA协议算法开始：ECDH-PSI（隐私求交）");
    return std::make_unique<PsiV2HandlerFactory>();
  } else if (ctx_->algo == org::interconnection::v2::ALGO_TYPE_SS_LR) {
    YACL_ENFORCE(!ctx_->protocol_families.empty());
    YACL_ENFORCE(ctx_->protocol_families.at(0) ==
                 org::interconnection::v2::PROTOCOL_FAMILY_SS);
    SPDLOG_INFO("BFIA协议算法开始：SS-LR（安全多方线性回归）");
    return std::make_unique<LrHandlerFactory>();
  } else if (ctx_->algo == org::interconnection::v2::ALGO_TYPE_SS_XGB) {
    YACL_ENFORCE(!ctx_->protocol_families.empty());
    YACL_ENFORCE(ctx_->protocol_families.at(0) ==
                 org::interconnection::v2::PROTOCOL_FAMILY_SS);
    SPDLOG_INFO("BFIA协议算法开始：SS-XGB（安全多方梯度提升树）");
    return std::make_unique<SsxgbHandlerFactory>();
  } else if (ctx_->algo == org::interconnection::v2::ALGO_TYPE_SS_PEARSONR) {
    YACL_ENFORCE(!ctx_->protocol_families.empty());
    YACL_ENFORCE(ctx_->protocol_families.at(0) ==
                 org::interconnection::v2::PROTOCOL_FAMILY_SS);
    SPDLOG_INFO("BFIA协议算法开始：SS-Pearson（安全相关性分析）");
    return std::make_unique<SsPearsonHandlerFactory>();
  } else if (ctx_->algo == org::interconnection::v2::ALGO_TYPE_SS_VIF) {
    YACL_ENFORCE(!ctx_->protocol_families.empty());
    YACL_ENFORCE(ctx_->protocol_families.at(0) ==
                 org::interconnection::v2::PROTOCOL_FAMILY_SS);
    SPDLOG_INFO("BFIA协议算法开始：SS-VIF（安全方差膨胀因子）");
    return std::make_unique<SsVifHandlerFactory>();
  } else if (ctx_->algo == org::interconnection::v2::ALGO_TYPE_SS_COMPARE) {
    YACL_ENFORCE(!ctx_->protocol_families.empty());
    YACL_ENFORCE(ctx_->protocol_families.at(0) ==
                 org::interconnection::v2::PROTOCOL_FAMILY_SS);
    SPDLOG_INFO("BFIA协议算法开始：SS-Compare（安全比较）");
    return std::make_unique<SsCompareHandlerFactory>();
  }
  SPDLOG_ERROR("BFIA协议算法处理器创建失败");
  return nullptr;
}

}  // namespace ic_impl
