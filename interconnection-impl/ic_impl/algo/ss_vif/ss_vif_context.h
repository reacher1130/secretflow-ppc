#pragma once
#include "ic_impl/context.h"
#include "ic_impl/protocol_family/ss/ss.h"
#include <unordered_map>
#include <vector>
#include <string>

namespace ic_impl::algo::ss_vif {

struct SsVifIoParam {
  int64_t sample_size{};
  std::vector<int32_t> feature_nums{};
  int32_t result_to_rank{-1};
  std::unordered_map<std::string, std::vector<std::string>> feature_select{};
};

struct SsVifContext {
  SsVifIoParam io_param;
  protocol_family::ss::TrustedThirdPartyConfig ttp_config;
  protocol_family::ss::SsProtocolParam ss_param;
  std::shared_ptr<IcContext> ic_ctx;
};

std::shared_ptr<SsVifContext> CreateSsVifContext(std::shared_ptr<IcContext>);

}
