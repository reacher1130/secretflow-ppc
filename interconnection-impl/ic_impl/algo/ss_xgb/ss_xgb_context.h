#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include "ic_impl/context.h"
#include "ic_impl/protocol_family/ss/ss.h"

namespace ic_impl::algo::ss_xgb {

struct SsxgbHyperParam {
    int64_t tree_num{};
    int64_t max_depth{};
    float lambda = 1.0f;
    float gamma = 0.0f;
    float learning_rate = 0.1f;
    float base_score = 0.5f;
    int seed = 42;
    int objective = 1; // 1: logistic, 2: linear
    // 额外超参：分位近似与列采样
    float sketch_eps = 0.08f;          // 近似精度，默认 0.08
    float col_sample_by_tree = 1.0f;   // 每棵树的列采样率，默认全列
};

struct SsxgbIoParam {
    int64_t sample_size{};
    std::vector<int32_t> feature_nums{};
    int32_t label_rank = -1;
    std::string label_name{};
    std::unordered_map<std::string, std::vector<std::string>> feature_select{};
    // 分桶等参数
    int num_bins = 16;
};

struct SsxgbContext {
    SsxgbHyperParam algo_param;
    SsxgbIoParam io_param;
    ic_impl::protocol_family::ss::TrustedThirdPartyConfig ttp_config;
    ic_impl::protocol_family::ss::SsProtocolParam ss_param;
    std::shared_ptr<ic_impl::IcContext> ic_ctx;
    // 数据缓存
    std::vector<std::vector<float>> X;
    std::vector<float> Y;
    // 辅助方法
    bool HasLabel() const {
        return static_cast<int32_t>(ic_ctx->lctx->Rank()) == io_param.label_rank;
    }
    void PrepareData();
};

std::shared_ptr<SsxgbContext> CreateSsxgbContext(std::shared_ptr<ic_impl::IcContext>);

} // namespace ic_impl::algo::ss_xgb