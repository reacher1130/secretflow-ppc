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

#include "ic_impl/algo/lr/lr_context.h"

#include "gflags/gflags.h"
#include "nlohmann/json.hpp"

#include "ic_impl/op/sigmoid/sigmoid.h"
#include "ic_impl/util.h"

#include "interconnection/handshake/algos/lr.pb.h"

DEFINE_bool(has_label, false, "if true, label is the last column of dataset");
DEFINE_int64(batch_size, 21, "size of each batch");
DEFINE_string(last_batch_policy, "discard",
              "policy to process the partial last batch of each epoch");
DEFINE_int64(num_epoch, 1, "number of epoch");
DEFINE_double(l0_norm, 0.0, "l0 norm");
DEFINE_double(l1_norm, 0.0, "l1 norm");
DEFINE_double(l2_norm, 0.5, "l2 norm");

DECLARE_bool(disable_handshake);

namespace ic_impl::algo::lr {

namespace {

int64_t SuggestedNumEpoch() {
  return util::GetParamEnv("num_epoch", FLAGS_num_epoch);
}

int64_t SuggestedBatchSize() {
  return util::GetParamEnv("batch_size", FLAGS_batch_size);
}

int32_t SuggestedLastBatchPolicy() {
  return util::GetFlagValue(
      org::interconnection::v2::algos::LastBatchPolicy_descriptor(),
      "LAST_BATCH_POLICY_",
      util::GetParamEnv("last_batch_policy", FLAGS_last_batch_policy));
}

double SuggestedL0Norm() { return util::GetParamEnv("l0_norm", FLAGS_l0_norm); }

double SuggestedL1Norm() { return util::GetParamEnv("l1_norm", FLAGS_l1_norm); }

double SuggestedL2Norm() { return util::GetParamEnv("l2_norm", FLAGS_l2_norm); }

LrHyperParam SuggestedLrHyperParam() {
  LrHyperParam lr_param;
  lr_param.num_epoch = SuggestedNumEpoch();
  lr_param.batch_size = SuggestedBatchSize();
  lr_param.last_batch_policy = SuggestedLastBatchPolicy();
  lr_param.l0_norm = SuggestedL0Norm();
  lr_param.l1_norm = SuggestedL1Norm();
  lr_param.l2_norm = SuggestedL2Norm();

  return lr_param;
}

int32_t GetLabelRank(const std::shared_ptr<IcContext>& ic_ctx) {
  SPDLOG_INFO("**********FLAGS_disable_handshake is {}**********", FLAGS_disable_handshake);
  if (!FLAGS_disable_handshake) {
    return FLAGS_has_label ? ic_ctx->lctx->Rank() : -1;
  }

  // get parameters from ENV
  char* label_owner = util::GetParamEnv("label_owner");
  YACL_ENFORCE(label_owner != nullptr, "label_owner not in ENV");

  std::size_t word_size = ic_ctx->lctx->WorldSize();
  for (std::size_t i = 0; i < word_size; ++i) {
    if (ic_ctx->lctx->PartyIdByRank(i) == label_owner) {
      return i;
    }
  }

  YACL_THROW("get label_rank failed");
}

std::unordered_map<std::string, std::vector<std::string>> GetFeatureSelect(
    const std::shared_ptr<IcContext>& ic_ctx) {
  std::unordered_map<std::string, std::vector<std::string>> feature_select;
  // 从环境变量中获取参数
  char* json_str = util::GetParamEnv("feature_select");
  if (json_str == nullptr) {
    SPDLOG_INFO("**********feature_select not in ENV**********");
    return feature_select;  // 没有获取到参数，返回空
  }

  auto json_object = nlohmann::json::parse(json_str);
  for (auto& [party_id, features] : json_object.items()) {
    feature_select[party_id] = features.get<std::vector<std::string>>();
  }

  // // 日志输出feature_select的内容
  // std::stringstream ss;
  // for (const auto& [party_id, feature_list] : feature_select) {
  //   ss << "Party ID: " << party_id << ", Features: [";
  //   for (std::size_t i = 0; i < feature_list.size(); ++i) {
  //     ss << feature_list[i];
  //     if (i < feature_list.size() - 1) {
  //       ss << ", ";
  //     }
  //   }
  //   ss << "]" << std::endl;
  // }
  // SPDLOG_INFO("feature_select is:\n{}", ss.str());
  // 输出
  return feature_select;
}

std::vector<int32_t> GetFeatureNums(const std::shared_ptr<IcContext>& ic_ctx) {
  std::vector<int32_t> feature_nums;
  if (!FLAGS_disable_handshake) {
    return feature_nums;  // got through the handshake that follows
  }

  auto feature_select = GetFeatureSelect(ic_ctx);
  std::size_t world_size = ic_ctx->lctx->WorldSize();
  feature_nums.resize(world_size);

  if (!feature_select.empty()) {  // get parameters from feature_select env
    for (std::size_t i = 0; i < world_size; ++i) {
      feature_nums.at(i) =
          feature_select[ic_ctx->lctx->PartyIdByRank(i)].size();
    }
  } else {  // get parameters from ENV
    char* json_str = util::GetParamEnv("feature_nums");
    YACL_ENFORCE(json_str != nullptr, "feature_nums not in ENV");
    auto json_object = nlohmann::json::parse(json_str);
    for (std::size_t i = 0; i < world_size; ++i) {
      feature_nums.at(i) = json_object.at(ic_ctx->lctx->PartyIdByRank(i));
    }
  }

  // // 日志输出feature_nums的内容
  // std::stringstream ss;
  // for (const auto& num : feature_nums) {
  //   ss << num;
  //   if (&num !=
  //       &feature_nums.back()) {  // 如果不是最后一个元素，则添加逗号分隔符
  //     ss << ", ";
  //   }
  // }
  // SPDLOG_INFO("feature_nums is:\n{}", ss.str());
  return feature_nums;
}

std::string GetLabelName() {
  std::string label_name;
  if (!FLAGS_disable_handshake) {  // 打开握手开关
    return label_name;             // got through the handshake that follows
  }
  // get parameters from ENV
  char* env_str = util::GetParamEnv("label_name");
  YACL_ENFORCE(env_str != nullptr, "label_name not in ENV");
  label_name = env_str;
  return label_name;
}

}  // namespace

std::shared_ptr<LrContext> CreateLrContext(std::shared_ptr<IcContext> ic_ctx) {
  auto ctx = std::make_shared<LrContext>();
  ctx->lr_param = SuggestedLrHyperParam();
  ctx->io_param.label_rank = GetLabelRank(ic_ctx);
  ctx->io_param.label_name = GetLabelName();
  ctx->io_param.feature_select = GetFeatureSelect(ic_ctx);
  ctx->io_param.feature_nums = GetFeatureNums(ic_ctx);
  ctx->sigmoid_mode = op::sigmoid::SuggestedSigmoidMode();
  ctx->ttp_config = protocol_family::ss::SuggestedTtpConfig();
  ctx->ss_param = protocol_family::ss::SuggestedSsProtocolParam();
  ctx->optimizer = optimizer::SuggestedOptimizer();
  ctx->ic_ctx = std::move(ic_ctx);

  return ctx;
}

}  // namespace ic_impl::algo::lr
