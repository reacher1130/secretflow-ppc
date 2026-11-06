#include "ss_pearson_context.h"
#include "ic_impl/util.h"
#include "ic_impl/protocol_family/ss/ss.h"
#include "gflags/gflags.h"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

DECLARE_bool(disable_handshake);

namespace ic_impl::algo::ss_pearson {
namespace util_ns = ic_impl::util;

namespace {

std::unordered_map<std::string, std::vector<std::string>> GetFeatureSelect(
    const std::shared_ptr<IcContext>& ic_ctx) {
  std::unordered_map<std::string, std::vector<std::string>> feature_select;
  char* json_str = util_ns::GetParamEnv("feature_select");
  if (json_str == nullptr) {
    SPDLOG_INFO("feature_select not in ENV");
    return feature_select;
  }
  auto json_object = nlohmann::json::parse(json_str);
  for (auto& [party_id, features] : json_object.items()) {
    feature_select[party_id] = features.get<std::vector<std::string>>();
  }
  return feature_select;
}

std::vector<int32_t> GetFeatureNums(const std::shared_ptr<IcContext>& ic_ctx) {
  std::vector<int32_t> feature_nums;
  if (!FLAGS_disable_handshake) {
    return feature_nums;
  }
  auto feature_select = GetFeatureSelect(ic_ctx);
  std::size_t world_size = ic_ctx->lctx->WorldSize();
  feature_nums.resize(world_size);
  if (!feature_select.empty()) {
    for (std::size_t i = 0; i < world_size; ++i) {
      feature_nums.at(i) = static_cast<int32_t>(
          feature_select[ic_ctx->lctx->PartyIdByRank(i)].size());
    }
  } else {
    char* json_str = util_ns::GetParamEnv("feature_nums");
    YACL_ENFORCE(json_str != nullptr, "feature_nums not in ENV");
    auto json_object = nlohmann::json::parse(json_str);
    for (std::size_t i = 0; i < world_size; ++i) {
      feature_nums.at(i) = json_object.at(ic_ctx->lctx->PartyIdByRank(i));
    }
  }
  return feature_nums;
}

int64_t GetSampleSize() {
  if (!FLAGS_disable_handshake) {
    return 0;  // will be set by handshake
  }
  return util_ns::GetParamEnv("sample_size", static_cast<int64_t>(0));
}

int32_t GetResultToRank(const std::shared_ptr<IcContext>& ic_ctx) {
  return util_ns::GetParamEnv("result_to_rank", static_cast<int32_t>(-1));
}

}  // namespace

std::shared_ptr<SsPearsonContext> CreateSsPearsonContext(
    std::shared_ptr<IcContext> ic_ctx) {
  auto ctx = std::make_shared<SsPearsonContext>();
  ctx->io_param.sample_size = GetSampleSize();
  ctx->io_param.feature_nums = GetFeatureNums(ic_ctx);
  ctx->io_param.result_to_rank = GetResultToRank(ic_ctx);
  ctx->io_param.feature_select = GetFeatureSelect(ic_ctx);
  ctx->ss_param = ic_impl::protocol_family::ss::SuggestedSsProtocolParam();
  ctx->ttp_config = ic_impl::protocol_family::ss::SuggestedTtpConfig();
  ctx->ic_ctx = std::move(ic_ctx);
  return ctx;
}

}  // namespace ic_impl::algo::ss_pearson


