#include "ss_xgb_context.h"
#include "ic_impl/protocol_family/ss/ss.h"
#include "ic_impl/util.h"

#include <fstream>
#include <sstream>

#include "gflags/gflags.h"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

DECLARE_bool(disable_handshake);
// `has_label` is defined by the LR context and shared by the single ic_main
// binary.  SS-XGB also needs it while building its handshake proposal so the
// label owner is discoverable before the negotiated label_rank is available.
DECLARE_bool(has_label);

namespace ic_impl::algo::ss_xgb {

using ic_impl::IcContext;
namespace util = ic_impl::util;

namespace {

// ---------- Hyper params (env with defaults) ----------
int64_t SuggestedNumBoostRound() { return util::GetParamEnv("num_boost_round", static_cast<int64_t>(3)); }

int64_t SuggestedMaxDepth() { return util::GetParamEnv("max_depth", static_cast<int64_t>(4)); }

double SuggestedLearningRate() { return util::GetParamEnv("learning_rate", 0.1); }

double SuggestedRegLambda() { return util::GetParamEnv("reg_lambda", 1.0); }

double SuggestedGamma() { return util::GetParamEnv("gamma", 0.0); }

double SuggestedBaseScore() { return util::GetParamEnv("base_score", 0.5); }

int64_t SuggestedSeed() { return util::GetParamEnv("seed", static_cast<int64_t>(42)); }

int32_t SuggestedObjective() { return util::GetParamEnv("objective", static_cast<int32_t>(1)); }

int32_t SuggestedNumBins() { return util::GetParamEnv("num_bins", static_cast<int32_t>(16)); }

double SuggestedSketchEps() { return util::GetParamEnv("sketch_eps", 0.08); }

double SuggestedColSampleByTree() { return util::GetParamEnv("colsample_by_tree", 1.0); }

SsxgbHyperParam SuggestedSsxgbHyperParam() {
    SsxgbHyperParam p;
    p.tree_num = SuggestedNumBoostRound();
    p.max_depth = SuggestedMaxDepth();
    p.learning_rate = static_cast<float>(SuggestedLearningRate());
    p.lambda = static_cast<float>(SuggestedRegLambda());
    p.gamma = static_cast<float>(SuggestedGamma());
    p.base_score = static_cast<float>(SuggestedBaseScore());
    p.seed = static_cast<int>(SuggestedSeed());
    p.objective = SuggestedObjective();
    p.sketch_eps = static_cast<float>(SuggestedSketchEps());
    p.col_sample_by_tree = static_cast<float>(SuggestedColSampleByTree());
    return p;
}

// ---------- IO params (env when handshake disabled) ----------
int32_t GetLabelRank(const std::shared_ptr<IcContext>& ic_ctx) {
    if (!FLAGS_disable_handshake) {
        return FLAGS_has_label ? static_cast<int32_t>(ic_ctx->lctx->Rank()) : -1;
    }

    char* label_owner = util::GetParamEnv("label_owner");
    YACL_ENFORCE(label_owner != nullptr, "label_owner not in ENV");

    std::size_t world_size = ic_ctx->lctx->WorldSize();
    for (std::size_t i = 0; i < world_size; ++i) {
        if (ic_ctx->lctx->PartyIdByRank(i) == label_owner) {
            return static_cast<int32_t>(i);
        }
    }
    YACL_THROW("get label_rank failed");
}

std::string GetLabelName() {
    char* env_str = util::GetParamEnv("label_name");
    return env_str == nullptr ? std::string{} : std::string(env_str);
}

std::unordered_map<std::string, std::vector<std::string>> GetFeatureSelect(
    const std::shared_ptr<IcContext>& ic_ctx) {
    std::unordered_map<std::string, std::vector<std::string>> feature_select;
    char* json_str = util::GetParamEnv("feature_select");
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
            feature_nums.at(i) =
                static_cast<int32_t>(feature_select[ic_ctx->lctx->PartyIdByRank(i)].size());
        }
    } else {
        char* json_str = util::GetParamEnv("feature_nums");
        YACL_ENFORCE(json_str != nullptr, "feature_nums not in ENV");
        auto json_object = nlohmann::json::parse(json_str);
        for (std::size_t i = 0; i < world_size; ++i) {
            feature_nums.at(i) = json_object.at(ic_ctx->lctx->PartyIdByRank(i));
        }
    }
    return feature_nums;
}

}  // namespace

// 目前通过握手/环境变量设置参数，无需解析外部配置

void SsxgbContext::PrepareData() {
    // 与 Demo 的读取逻辑对齐：
    // - 首行表头逗号分隔；
    // - 数据行为空格分隔数值；
    std::ifstream fin(util::GetInputFileName("data/perfect_logit_a.csv"));
    if (!fin.is_open()) {
        SPDLOG_WARN("open input file failed, fallback to default data.csv");
        fin.open("data.csv");
    }
    std::string line;
    if (!std::getline(fin, line)) {
        return;
    }
    while (std::getline(fin, line)) {
        std::istringstream iss(line);
        std::vector<float> row;
        float val;
        while (iss >> val) row.push_back(val);
        X.push_back(row);
    }
}

std::shared_ptr<SsxgbContext> CreateSsxgbContext(std::shared_ptr<IcContext> ic_ctx) {
    auto ctx = std::make_shared<SsxgbContext>();
    // algo
    ctx->algo_param = SuggestedSsxgbHyperParam();
    // io
    ctx->io_param.label_rank = GetLabelRank(ic_ctx);
    ctx->io_param.label_name = GetLabelName();
    ctx->io_param.feature_select = GetFeatureSelect(ic_ctx);
    ctx->io_param.feature_nums = GetFeatureNums(ic_ctx);
    ctx->io_param.num_bins = SuggestedNumBins();
    // protocol / ttp：始终使用建议值，握手过程中按需更新
    ctx->ss_param = ic_impl::protocol_family::ss::SuggestedSsProtocolParam();
    ctx->ttp_config = ic_impl::protocol_family::ss::SuggestedTtpConfig();
    // misc
    ctx->ic_ctx = std::move(ic_ctx);
    return ctx;
}

} // namespace ic_impl::algo::ss_xgb
