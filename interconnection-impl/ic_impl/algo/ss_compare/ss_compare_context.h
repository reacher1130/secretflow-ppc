#pragma once
#include "ic_impl/context.h"
#include "ic_impl/protocol_family/ss/ss.h"
#include <unordered_map>
#include <vector>
#include <string>

namespace ic_impl::algo::ss_compare {

struct SSCompareIoParam {
  int64_t sample_size{};
  std::vector<int32_t> feature_nums{};
  int32_t result_to_rank{-1};  // -1表示所有方都输出
  int32_t compare_column_idx{-1};  // 要比较的列索引（全局列索引）
  int32_t compare_column_idx_b{-1};  // 第二个要比较的列索引（全局列索引，用于两列比较）
                                     // 如果 >= 0，则进行两列比较（compare_column_idx vs compare_column_idx_b）
                                     // 如果 < 0，则进行单列与均值比较（compare_column_idx vs 均值）
};

struct SSCompareContext {
  SSCompareIoParam io_param;
  protocol_family::ss::TrustedThirdPartyConfig ttp_config;
  protocol_family::ss::SsProtocolParam ss_param;
  int32_t compare_op{0};  // 0: 大于, 1: 小于, 2: 等于, 3: 大于等于, 4: 小于等于
  std::shared_ptr<IcContext> ic_ctx;
};

std::shared_ptr<SSCompareContext> CreateSSCompareContext(std::shared_ptr<IcContext>);

}

