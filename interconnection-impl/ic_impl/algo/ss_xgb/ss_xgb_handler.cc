#include "ic_impl/numeric_output.h"
#include "libspu/mpc/semi2k/type.h"
#include "xtensor/xadapt.hpp"
#include <functional>
#include <cmath>
#include "ss_xgb_handler.h"
#include <set>
#include <iostream>
#include <fstream>
#include <sstream>
#include <numeric>
#include <chrono>
#include "spdlog/spdlog.h"
#include "yacl/base/exception.h"
#include "interconnection/handshake/algos/ss_xgb.pb.h"
#include "libspu/core/config.h"
#include "libspu/core/encoding.h"
#include "libspu/kernel/hal/constants.h"
#include "libspu/kernel/hal/polymorphic.h"
#include "libspu/kernel/hal/public_helper.h"
#include "libspu/kernel/hal/shape_ops.h"
#include "libspu/kernel/hal/type_cast.h"
#include "libspu/mpc/factory.h"
#include "xtensor/xarray.hpp"
#include "xtensor/xcsv.hpp"
#include "xtensor/xview.hpp"
#include "gflags/gflags.h"
#include "ic_impl/util.h"
#include <unordered_map>
#include <filesystem>

// gflags 的声明需要在全局命名空间，避免链接符号带上命名空间前缀
DECLARE_bool(disable_handshake);
DECLARE_string(dataset);
DECLARE_int32(skip_rows);
// 同步固定分裂阈值，避免控制流不一致
DEFINE_bool(ssxgb_sync_fixed_splits, false, "use fixed, synchronized splits");
DEFINE_int32(ssxgb_num_bins, 8, "number of bins for fixed splits");

namespace ic_impl::algo::ss_xgb {

using ic_impl::HandshakeRequestV2;
using ic_impl::HandshakeResponseV2;

using namespace org::interconnection::v2;


namespace {

// 复用 LR 的读取思路：从 util::GetInputFileName(FLAGS_dataset) 读取 CSV，首行逗号分隔表头
// 返回数据矩阵，并填充 header->index 的映射；数据体用逗号分隔，支持 FLAGS_skip_rows
std::unique_ptr<xt::xarray<float>> ReadDataset(
    std::unordered_map<std::string, std::size_t>& headers_index_map) {
    std::string input_file = ic_impl::util::GetInputFileName(FLAGS_dataset);
    std::ifstream file(input_file);
    YACL_ENFORCE(file, "open file={} failed", input_file);

    std::string header_line;
    std::getline(file, header_line);
    std::istringstream header_stream(header_line);
    std::string header;
    std::size_t i = 0;
    while (std::getline(header_stream, header, ',')) {
        headers_index_map[header] = i++;
    }

    return std::make_unique<xt::xarray<float>>(
        xt::load_csv<float>(file, ',', FLAGS_skip_rows));
}

}  // namespace


SsxgbHandler::SsxgbHandler(std::shared_ptr<SsxgbContext> ctx)
    : AlgoV2Handler(ctx->ic_ctx), ctx_(std::move(ctx)) {
    // 初始化参数
    tree_num_ = ctx_->algo_param.tree_num;
    max_depth_ = ctx_->algo_param.max_depth;
    lambda_ = ctx_->algo_param.lambda;
    gamma_ = ctx_->algo_param.gamma;
    sctx_ = nullptr; // TODO: 传入实际的SPUContext
}

SsxgbHandler::~SsxgbHandler() = default;

bool SsxgbHandler::PrepareDataset() {
    // 与 SS-LR 对齐的数据读取：从 ENV/FLAGS 定位文件，首行逗号分隔表头，数据为标准 CSV
    SPDLOG_INFO("**********SS-XGB preparing dataset**********");
    std::unordered_map<std::string, std::size_t> headers_index_map;
    auto dataset = ReadDataset(headers_index_map);
    YACL_ENFORCE(dataset->shape().size() == 2);

    int64_t n = dataset->shape(0);
    int64_t m = dataset->shape(1);
    YACL_ENFORCE(n > 0);
    ctx_->io_param.sample_size = static_cast<int>(n);

    int self_rank = ctx_->ic_ctx->lctx->Rank();
    std::string self_party_id = ctx_->ic_ctx->lctx->PartyIdByRank(self_rank);

    // 计算本方特征列数：优先使用 feature_select；否则（不推荐）按是否持标扣除 1
    int32_t feature_num = 0;
    const auto& feature_select = ctx_->io_param.feature_select;
    if (!feature_select.empty() && feature_select.count(self_party_id)) {
        feature_num = static_cast<int32_t>(feature_select.at(self_party_id).size());
    } else {
        feature_num = ctx_->HasLabel() ? static_cast<int32_t>(m - 1) : static_cast<int32_t>(m);
    }

    if (FLAGS_disable_handshake) {
        YACL_ENFORCE(ctx_->io_param.feature_nums.at(self_rank) == feature_num,
                     "feature_num mismatch with ENV");
    } else {
        YACL_ENFORCE(feature_num > 0);
        ctx_->io_param.feature_nums.resize(ctx_->ic_ctx->lctx->WorldSize());
        ctx_->io_param.feature_nums.at(self_rank) = feature_num;
    }

    // 输出数据集尺寸信息
    SPDLOG_INFO("[SS-XGB] 数据集尺寸: {} 行 x {} 列 (样本数: {}, 特征数: {})", 
                n, m, n, feature_num);

    // 装载本方特征到 ctx_->X/ctx_->Y
    std::vector<std::size_t> feature_col_indices;
    if (!feature_select.empty() && feature_select.count(self_party_id)) {
        feature_col_indices.reserve(feature_select.at(self_party_id).size());
        for (const auto& name : feature_select.at(self_party_id)) {
            auto it = headers_index_map.find(name);
            YACL_ENFORCE(it != headers_index_map.end(), "feature {} not found", name);
            feature_col_indices.push_back(it->second);
        }
    } else {
        // 默认：若持标，则排除标签列；否则使用全量列
        feature_col_indices.reserve(static_cast<std::size_t>(m));
        if (ctx_->HasLabel()) {
            std::string label_name = ctx_->io_param.label_name;
            YACL_ENFORCE(!label_name.empty(), "label_name is empty");
            YACL_ENFORCE(headers_index_map.count(label_name) > 0, "label {} not found", label_name);
            std::size_t label_idx = headers_index_map[label_name];
            for (std::size_t j = 0; j < static_cast<std::size_t>(m); ++j) {
                if (j == label_idx) continue;
                feature_col_indices.push_back(j);
            }
        } else {
            for (std::size_t j = 0; j < static_cast<std::size_t>(m); ++j) {
                feature_col_indices.push_back(j);
            }
        }
    }

    ctx_->X.clear();
    ctx_->X.reserve(static_cast<std::size_t>(n));
    for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
        std::vector<float> row;
        row.reserve(feature_col_indices.size());
        for (auto col : feature_col_indices) {
            row.push_back((*dataset)(i, col));
        }
        ctx_->X.push_back(std::move(row));
    }

    ctx_->Y.clear();
    if (ctx_->HasLabel()) {
        std::string label_name = ctx_->io_param.label_name;
        YACL_ENFORCE(!label_name.empty(), "label_name is empty");
        YACL_ENFORCE(headers_index_map.count(label_name) > 0, "label {} not found", label_name);
        std::size_t label_idx = headers_index_map[label_name];
        ctx_->Y.reserve(static_cast<std::size_t>(n));
        for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
            ctx_->Y.push_back((*dataset)(i, label_idx));
        }
    } else {
        ctx_->Y.assign(static_cast<std::size_t>(n), 0.0f);
    }

    SPDLOG_INFO("[SS-XGB] dataset prepared: samples={}, input_cols={}, features_selected={}, label_owner_rank={}, has_label={}",
                n, m, feature_num, ctx_->io_param.label_rank, ctx_->HasLabel());

    return true;
}

HandshakeRequestV2 SsxgbHandler::BuildHandshakeRequest() {
    HandshakeRequestV2 req;
    req.set_version(2);
    req.set_requester_rank(ctx_->ic_ctx->lctx->Rank());
    req.add_supported_algos(ctx_->ic_ctx->algo);
    algos::SSXGBParamsProposal ssxgb_proposal;
    ssxgb_proposal.add_supported_versions(1);
    ssxgb_proposal.set_support_col_sample_by_tree(false);
    req.add_algo_params()->PackFrom(ssxgb_proposal);
    for (const auto& family : ctx_->ic_ctx->protocol_families) {
        if (family == PROTOCOL_FAMILY_SS) {
            req.add_protocol_families(family);
            org::interconnection::v2::protocol::SSProtocolProposal ss_proposal;
            ss_proposal.add_supported_versions(1);
            ss_proposal.add_supported_protocols(ctx_->ss_param.protocol);
            ss_proposal.add_field_types(ctx_->ss_param.field_type);
            ss_proposal.add_shard_serialize_formats(ctx_->ss_param.shard_serialize_format);
            // trunc mode
            auto* trunc_mode = ss_proposal.add_trunc_modes();
            trunc_mode->add_supported_versions(1);
            trunc_mode->set_method(org::interconnection::v2::protocol::TRUNC_MODE_PROBABILISTIC);
            // PRG config
            auto* prg_cfg = ss_proposal.add_prg_configs();
            prg_cfg->add_supported_versions(1);
            prg_cfg->set_crypto_type(org::interconnection::v2::protocol::CRYPTO_TYPE_AES128_CTR);
            // TTP参数
            auto* ttp_param = ss_proposal.add_triple_configs();
            ttp_param->add_supported_versions(1);
            ttp_param->set_sever_version(ctx_->ttp_config.ttp_server_version);
            req.add_protocol_family_params()->PackFrom(ss_proposal);
        } else {
            YACL_THROW("Protocol family {} for SS-XGB not implemented", family);
        }
    }
    // IO参数
    algos::SSXGBDataIoProposal io_proposal;
    io_proposal.add_supported_versions(1);
    io_proposal.set_sample_size(ctx_->io_param.sample_size);
    // feature_num: 若握手开启，需在 PrepareDataset 中写入本方；若关闭，则从 ENV 已写入
    if (ctx_->io_param.feature_nums.size() <= static_cast<std::size_t>(ctx_->ic_ctx->lctx->Rank())) {
        // 当缺失时，按本地已加载特征数兜底
        int self_rank = ctx_->ic_ctx->lctx->Rank();
        int feature_num_local = ctx_->X.empty() ? 0 : static_cast<int>(ctx_->X[0].size());
        if (!FLAGS_disable_handshake) {
            ctx_->io_param.feature_nums.resize(ctx_->ic_ctx->lctx->WorldSize());
        }
        if (static_cast<int32_t>(ctx_->io_param.feature_nums.size()) > self_rank) {
            ctx_->io_param.feature_nums[self_rank] = feature_num_local;
        }
    }
    io_proposal.set_feature_num(ctx_->io_param.feature_nums.empty() ? 0 : ctx_->io_param.feature_nums[ctx_->ic_ctx->lctx->Rank()]);
    io_proposal.set_has_label(ctx_->HasLabel());
    req.mutable_io_param()->PackFrom(io_proposal);
    return req;
}

HandshakeResponseV2 SsxgbHandler::BuildHandshakeResponse() {
    HandshakeResponseV2 resp;
    resp.mutable_header()->set_error_code(org::interconnection::OK);
    resp.set_algo(ALGO_TYPE_SS_XGB);
    algos::SSXGBParamsResult ssxgb_result;
    ssxgb_result.set_version(1);
    ssxgb_result.set_num_boost_round(ctx_->algo_param.tree_num);
    ssxgb_result.set_max_depth(ctx_->algo_param.max_depth);
    ssxgb_result.set_learning_rate(ctx_->algo_param.learning_rate);
    ssxgb_result.set_sketch_eps(ctx_->algo_param.sketch_eps);
    ssxgb_result.add_objective(ctx_->algo_param.objective);
    ssxgb_result.set_reg_lambda(ctx_->algo_param.lambda);
    ssxgb_result.set_base_score(ctx_->algo_param.base_score);
    ssxgb_result.set_seed(ctx_->algo_param.seed);
    ssxgb_result.set_col_sample_by_tree(ctx_->algo_param.col_sample_by_tree);
    resp.mutable_algo_param()->PackFrom(ssxgb_result);
    org::interconnection::v2::protocol::SSProtocolResult ss_result;
    ss_result.set_version(1);
    ss_result.set_protocol(ctx_->ss_param.protocol);
    ss_result.set_field_type(ctx_->ss_param.field_type);
    ss_result.set_fxp_fraction_bits(ctx_->ss_param.fxp_bits);
    // 返回截断模式与分片序列化格式，保持与 proto 一致
    ss_result.mutable_trunc_mode()->set_version(1);
    ss_result.mutable_trunc_mode()->set_method(static_cast<org::interconnection::v2::protocol::TruncMode>(ctx_->ss_param.trunc_mode));
    ss_result.set_shard_serialize_format(ctx_->ss_param.shard_serialize_format);
    // TTP参数
    auto* ttp_result = ss_result.mutable_triple_config();
    ttp_result->set_version(1);
    ttp_result->set_server_host(ctx_->ttp_config.ttp_server_host);
    ttp_result->set_sever_version(ctx_->ttp_config.ttp_server_version);
    ttp_result->set_adjust_rank(ctx_->ttp_config.ttp_adjust_rank);
    resp.add_protocol_families(PROTOCOL_FAMILY_SS);
    resp.add_protocol_family_params()->PackFrom(ss_result);
    // IO参数
    algos::SSXGBDataIoResult io_result;
    io_result.set_version(1);
    io_result.set_sample_size(ctx_->io_param.sample_size);
    for (auto f : ctx_->io_param.feature_nums) io_result.add_feature_nums(f);
    io_result.set_label_rank(ctx_->io_param.label_rank);
    resp.mutable_io_param()->PackFrom(io_result);
    return resp;
}

bool SsxgbHandler::ProcessHandshakeResponse(const HandshakeResponseV2& resp) {
    if (!AlgoV2Handler::ProcessHandshakeResponse(resp)) {
        return false;
    }
    
    YACL_ENFORCE(resp.algo() == ALGO_TYPE_SS_XGB);
    
    algos::SSXGBParamsResult ssxgb_result;
    if (resp.algo_param().UnpackTo(&ssxgb_result)) {
        ctx_->algo_param.tree_num = ssxgb_result.num_boost_round();
        ctx_->algo_param.max_depth = ssxgb_result.max_depth();
        ctx_->algo_param.learning_rate = ssxgb_result.learning_rate();
        ctx_->algo_param.objective = ssxgb_result.objective_size() > 0 ? ssxgb_result.objective(0) : 1;
        ctx_->algo_param.lambda = ssxgb_result.reg_lambda();
        ctx_->algo_param.base_score = ssxgb_result.base_score();
        ctx_->algo_param.seed = ssxgb_result.seed();
        // 读取列采样与分位精度（proto3 标量无 presence，直接取值）
        ctx_->algo_param.col_sample_by_tree = ssxgb_result.col_sample_by_tree();
        ctx_->algo_param.sketch_eps = ssxgb_result.sketch_eps();
    }
    
    org::interconnection::v2::protocol::SSProtocolResult ss_result;
    if (resp.protocol_family_params_size() > 0 && resp.protocol_family_params(0).UnpackTo(&ss_result)) {
        ctx_->ss_param.protocol = ss_result.protocol();
        ctx_->ss_param.field_type = ss_result.field_type();
        ctx_->ss_param.fxp_bits = ss_result.fxp_fraction_bits();
        ctx_->ss_param.shard_serialize_format = ss_result.shard_serialize_format();
        if (ss_result.has_trunc_mode()) {
            ctx_->ss_param.trunc_mode = ss_result.trunc_mode().method();
        }
        if (ss_result.has_triple_config()) {
            ctx_->ttp_config.ttp_server_version = ss_result.triple_config().sever_version();
            ctx_->ttp_config.ttp_server_host = ss_result.triple_config().server_host();
            ctx_->ttp_config.ttp_adjust_rank = ss_result.triple_config().adjust_rank();
        }
    }
    
    if (resp.has_io_param()) {
        algos::SSXGBDataIoResult io_result;
        if (resp.io_param().UnpackTo(&io_result)) {
            ctx_->io_param.sample_size = io_result.sample_size();
            ctx_->io_param.feature_nums.clear();
            for (int i = 0; i < io_result.feature_nums_size(); ++i) {
                ctx_->io_param.feature_nums.push_back(io_result.feature_nums(i));
            }
            ctx_->io_param.label_rank = io_result.label_rank();
        }
    }
    
    return true;
}

status::ErrorStatus SsxgbHandler::NegotiateHandshakeParams(
    const std::vector<HandshakeRequestV2>& requests) {
    auto status = NegotiateAlgoParams(requests);
    if (!status.ok()) {
        return status;
    }
    
    status = NegotiateProtocolParams(requests);
    if (!status.ok()) {
        return status;
    }
    
    status = NegotiateIoParams(requests);
    if (!status.ok()) {
        return status;
    }
    
    status = NegotiateTtpParams(requests);
    if (!status.ok()) {
        return status;
    }
    
    return status::OkStatus();
}

status::ErrorStatus SsxgbHandler::NegotiateIoParams(const std::vector<HandshakeRequestV2>& requests) {
    // 与 LR 对齐：校验 sample_size 一致；收集 feature_num；唯一 label_rank
    for (const auto& request : requests) {
        algos::SSXGBDataIoProposal io_proposal;
        if (!request.io_param().UnpackTo(&io_proposal)) {
            return status::InvalidRequestError("certain request has invalid io param");
        }

        // sample size 必须一致
        if (io_proposal.sample_size() != ctx_->io_param.sample_size) {
            return status::HandshakeRefusedError("sample size inconsistent");
        }

        // feature_num 必须为正，记录对端的 feature_num
        if (io_proposal.feature_num() <= 0) {
            return status::InvalidRequestError("certain request has invalid feature_num");
        } else {
            if (static_cast<int32_t>(ctx_->io_param.feature_nums.size()) <= request.requester_rank()) {
                ctx_->io_param.feature_nums.resize(ctx_->ic_ctx->lctx->WorldSize());
            }
            ctx_->io_param.feature_nums[request.requester_rank()] = io_proposal.feature_num();
        }

        // 唯一标签方
        if (io_proposal.has_label()) {
            if (ctx_->io_param.label_rank != -1 && ctx_->io_param.label_rank != request.requester_rank()) {
                return status::HandshakeRefusedError("more than one party have label");
            }
            ctx_->io_param.label_rank = request.requester_rank();
        }
    }

    if (ctx_->io_param.label_rank == -1) {
        return status::InvalidRequestError("no party has label");
    }

    // feature_nums 完整性校验
    if (ctx_->io_param.feature_nums.size() != ctx_->ic_ctx->lctx->WorldSize()) {
        return status::InvalidRequestError("feature_nums size mismatch world size");
    }
    for (int i = 0; i < static_cast<int>(ctx_->io_param.feature_nums.size()); ++i) {
        if (ctx_->io_param.feature_nums[i] <= 0) {
            return status::InvalidRequestError("feature_num not set properly");
        }
    }

    return status::OkStatus();
}

status::ErrorStatus SsxgbHandler::NegotiateTtpParams(const std::vector<HandshakeRequestV2>& requests) {
    // TTP 参数协商：检查版本一致性
    std::set<int32_t> ttp_versions;
    for (const auto& req : requests) {
        for (const auto& proto : req.protocol_family_params()) {
            org::interconnection::v2::protocol::SSProtocolProposal proposal;
            if (proto.UnpackTo(&proposal)) {
                for (int i = 0; i < proposal.triple_configs_size(); ++i) {
                    ttp_versions.insert(proposal.triple_configs(i).sever_version());
                }
            }
        }
    }
    if (ttp_versions.size() != 1) {
        return status::HandshakeRefusedError("TTP version inconsistent");
    }
    ctx_->ttp_config.ttp_server_version = *ttp_versions.begin();
    return status::OkStatus();
}

status::ErrorStatus SsxgbHandler::NegotiateAlgoParams(const std::vector<HandshakeRequestV2>& requests) {
    // 要求所有请求声明支持 SS-XGB，且 algo proposal 的版本包含 1
    for (const auto& req : requests) {
        bool support_algo = false;
        for (int i = 0; i < req.supported_algos_size(); ++i) {
            if (req.supported_algos(i) == ALGO_TYPE_SS_XGB) { support_algo = true; break; }
        }
        if (!support_algo) {
            return status::UnsupportedArgumentError("request not support SS-XGB");
        }

        bool ok = false;
        for (int i = 0; i < req.algo_params_size(); ++i) {
            algos::SSXGBParamsProposal prop;
            if (req.algo_params(i).UnpackTo(&prop)) {
                // 版本对齐：要求含 1
                bool has_v1 = false;
                for (int j = 0; j < prop.supported_versions_size(); ++j) {
                    has_v1 |= (prop.supported_versions(j) == 1);
                }
                if (!has_v1) {
                    return status::UnsupportedArgumentError("algo version not supported");
                }
                ok = true;
                break;
            }
        }
        if (!ok) {
            return status::InvalidRequestError("no valid ss-xgb algo params");
        }
    }

    return status::OkStatus();
}

status::ErrorStatus SsxgbHandler::NegotiateProtocolParams(const std::vector<HandshakeRequestV2>& requests) {
    // 参考 LR 的协商：检查协议、域、序列化格式、截断模式、PRG 类型及 TTP 版本
    for (const auto& req : requests) {
        bool has_ss = false;
        for (int i = 0; i < req.protocol_families_size(); ++i) {
            if (req.protocol_families(i) == PROTOCOL_FAMILY_SS) { has_ss = true; break; }
        }
        if (!has_ss) {
            return status::UnsupportedArgumentError("request not support SS protocol family");
        }

        bool ok = false;
        for (int i = 0; i < req.protocol_family_params_size(); ++i) {
            org::interconnection::v2::protocol::SSProtocolProposal prop;
            if (req.protocol_family_params(i).UnpackTo(&prop)) {
                // protocol
                bool protocol_ok = false;
                for (int j = 0; j < prop.supported_protocols_size(); ++j) {
                    protocol_ok |= (prop.supported_protocols(j) == ctx_->ss_param.protocol);
                }
                if (!protocol_ok) return status::UnsupportedArgumentError("protocol unsupported");

                // field type
                bool field_ok = false;
                for (int j = 0; j < prop.field_types_size(); ++j) {
                    field_ok |= (prop.field_types(j) == ctx_->ss_param.field_type);
                }
                if (!field_ok) return status::UnsupportedArgumentError("field type unsupported");

                // serialize format
                bool ser_ok = false;
                for (int j = 0; j < prop.shard_serialize_formats_size(); ++j) {
                    ser_ok |= (prop.shard_serialize_formats(j) == ctx_->ss_param.shard_serialize_format);
                }
                if (!ser_ok) return status::UnsupportedArgumentError("serialize format unsupported");

                // trunc mode: 允许 probabilistic
                bool trunc_ok = false;
                for (int j = 0; j < prop.trunc_modes_size(); ++j) {
                    if (prop.trunc_modes(j).method() == org::interconnection::v2::protocol::TRUNC_MODE_PROBABILISTIC) {
                        trunc_ok = true; break;
                    }
                }
                if (!trunc_ok) return status::UnsupportedArgumentError("trunc mode unsupported");

                // PRG config: 需支持 AES128_CTR
                bool prg_ok = false;
                for (int j = 0; j < prop.prg_configs_size(); ++j) {
                    if (prop.prg_configs(j).crypto_type() == org::interconnection::v2::protocol::CRYPTO_TYPE_AES128_CTR) {
                        prg_ok = true; break;
                    }
                }
                if (!prg_ok) return status::UnsupportedArgumentError("prg crypto unsupported");

                ok = true; break;
            }
        }

        if (!ok) return status::InvalidRequestError("no valid ss protocol params");
    }

    return status::OkStatus();
}

std::unique_ptr<spu::SPUContext> SsxgbHandler::MakeSpuContext() {
    static const std::map<int32_t, spu::ProtocolKind> protocol_map{
        {org::interconnection::v2::protocol::PROTOCOL_KIND_SEMI2K, spu::ProtocolKind::SEMI2K},
        {org::interconnection::v2::protocol::PROTOCOL_KIND_ABY3, spu::ProtocolKind::ABY3}
    };

    spu::RuntimeConfig config;
    config.set_protocol(protocol_map.at(ctx_->ss_param.protocol));
    config.set_field(static_cast<spu::FieldType>(ctx_->ss_param.field_type));
    config.set_fxp_fraction_bits(ctx_->ss_param.fxp_bits);
    // 与 LR 对齐的运行时细化项
    config.set_experimental_disable_mmul_split(true);
    config.set_experimental_disable_vectorization(true);
    if (ctx_->ss_param.trunc_mode == org::interconnection::v2::protocol::TRUNC_MODE_PROBABILISTIC) {
        config.set_trunc_allow_msb_error(true);
    }

    // 与 LR 保持一致的运行时初始化（包括 fxp_div_goldschmidt_iters 等）
    populateRuntimeConfig(config);

    // TTP配置：与 ss-lr 对齐，按 use_ttp 判定
    if (ctx_->ttp_config.use_ttp) {
        config.set_beaver_type(spu::RuntimeConfig_BeaverType_TrustedThirdParty);
        config.mutable_ttp_beaver_config()->set_server_host(ctx_->ttp_config.ttp_server_host);
        config.mutable_ttp_beaver_config()->set_asym_crypto_schema(ctx_->ttp_config.ttp_asym_crypto_schema);
        config.mutable_ttp_beaver_config()->set_server_public_key(ctx_->ttp_config.ttp_public_key);
        config.mutable_ttp_beaver_config()->set_adjust_rank(ctx_->ttp_config.ttp_adjust_rank);
    } else {
        config.set_beaver_type(spu::RuntimeConfig_BeaverType_TrustedFirstParty);
    }

    config.set_enable_action_trace(true);
    config.set_enable_type_checker(true);

    return std::make_unique<spu::SPUContext>(config, ctx_->ic_ctx->lctx);
}

void SsxgbHandler::RunAlgo() {
    namespace h = spu::kernel::hal;
    auto holder = MakeSpuContext();
    sctx_ = holder.get();
    spu::mpc::Factory::RegisterProtocol(sctx_, sctx_->lctx());
    YACL_ENFORCE(ctx_->ss_param.protocol == protocol::PROTOCOL_KIND_SEMI2K,
                 "SS-XGB currently supports SEMI2K only");
    const auto n = ctx_->io_param.sample_size;
    const auto rank = sctx_->lctx()->Rank();
    const auto world = sctx_->lctx()->WorldSize();
    YACL_ENFORCE(ctx_->algo_param.objective == 1 || ctx_->algo_param.objective == 2);
    YACL_ENFORCE(ctx_->algo_param.col_sample_by_tree == 1.0f,
                 "column sampling is not implemented; refusing to silently ignore it");
    // Owner input occupies its additive-share block; other ranks contribute zero.
    const auto ty = spu::makeType<spu::mpc::semi2k::AShrTy>(
        static_cast<spu::FieldType>(ctx_->ss_param.field_type));
    auto owned = [&](const std::vector<float>& data, const spu::Shape& shape) {
        auto buffer=data;
        auto arr = xt::adapt(buffer, std::vector<size_t>{static_cast<size_t>(shape[0]), static_cast<size_t>(shape[1])});
        spu::DataType dtype;
        auto encoded = spu::encodeToRing(spu::PtBufferView(arr),
            static_cast<spu::FieldType>(ctx_->ss_param.field_type), ctx_->ss_param.fxp_bits, &dtype);
        spu::Value v(encoded,dtype); v.storage_type()=ty; return v;
    };
    std::vector<spu::Value> parts;
    int features=0;
    for (size_t owner=0;owner<world;++owner) {
        const auto cols=ctx_->io_param.feature_nums.at(owner); features+=cols;
        std::vector<float> flat(n*cols,0.0f);
        if (rank==owner) for(int i=0;i<n;++i) for(int j=0;j<cols;++j) flat[i*cols+j]=ctx_->X.at(i).at(j);
        parts.push_back(owned(flat,{n,cols}));
    }
    auto X=h::concatenate(sctx_,parts,1);
    std::vector<float> labels(n,0.0f);
    if(static_cast<int>(rank)==ctx_->io_param.label_rank) labels=ctx_->Y;
    auto Y=owned(labels,{n,1});
    auto scalar=[&](float x){return h::constant(sctx_,x,spu::DT_F32,{1,1});};
    auto expand=[&](const spu::Value& x){return h::broadcast_to(sctx_,x,{n,1});};
    auto mul=[&](const spu::Value&a,const spu::Value&b){return h::mul(sctx_,a,b);};
    auto add=[&](const spu::Value&a,const spu::Value&b){return h::add(sctx_,a,b);};
    auto sub=[&](const spu::Value&a,const spu::Value&b){return h::sub(sctx_,a,b);};
    auto div=[&](const spu::Value&a,const spu::Value&b){return mul(a,h::reciprocal(sctx_,b));};
    auto ones=h::constant(sctx_,1.0f,spu::DT_F32,{n,1});
    auto sum=[&](const spu::Value&x){return h::matmul(sctx_,h::transpose(sctx_,ones),x);};
    auto sigmoid=[&](const spu::Value&x){return h::reciprocal(sctx_,add(ones,h::exp(sctx_,h::negate(sctx_,x))));};
    // Fixed-shape histogram evaluation avoids data-dependent control flow.
    // Grid endpoints, selected splits, model weights and masks stay secret.
    YACL_ENFORCE(ctx_->algo_param.gamma == 0.0f, "gamma is not negotiated by this BFIA schema");
    YACL_ENFORCE(ctx_->algo_param.sketch_eps > 0 && ctx_->algo_param.sketch_eps <= 0.5f);
    const int bins=std::max(4,static_cast<int>(std::ceil(1.0f/ctx_->algo_param.sketch_eps)));
    // Secure column extrema: an equally spaced histogram grid whose endpoints
    // are not revealed. Vectorized tree reduction keeps row-dependent values secret.
    auto extrema=[&](spu::Value v,bool minimum) {
        while(v.shape()[0]>1) {
            const auto rows=v.shape()[0]; const auto half=rows/2;
            auto previous=v;
            auto a=h::slice(sctx_,v,{0,0},{half,features},{});
            auto b=h::slice(sctx_,v,{half,0},{half*2,features},{});
            auto pick=h::less(sctx_,a,b);
            v=minimum?h::select(sctx_,pick,a,b):h::select(sctx_,pick,b,a);
            if(rows%2) {
                auto last=h::slice(sctx_,previous,{rows-1,0},{rows,features},{});
                v=h::concatenate(sctx_,{v,last},0);
            }
        }
        return v;
    };
    auto lower=extrema(X,true); auto upper=extrema(X,false);
    std::vector<spu::Value> candidates;
    for(int f=0;f<features;++f) {
        auto col=h::slice(sctx_,X,{0,f},{n,f+1},{});
        for(int b=1;b<bins;++b) {
            auto lo=h::slice(sctx_,lower,{0,f},{1,f+1},{});
            auto hi=h::slice(sctx_,upper,{0,f},{1,f+1},{});
            auto threshold=expand(add(lo,mul(sub(hi,lo),scalar(static_cast<float>(b)/bins))));
            candidates.push_back(h::dtype_cast(sctx_,h::less(sctx_,col,threshold),spu::DT_F32));
        }
    }
    float base=ctx_->algo_param.base_score;
    if(ctx_->algo_param.objective==1) {
        YACL_ENFORCE(base>0 && base<1,"logistic base_score must be a probability");
        base=std::log(base/(1.0f-base));
    }
    auto pred=h::seal(sctx_,expand(scalar(base)));
    std::vector<spu::Value> model;
    model.push_back(h::reshape(sctx_,lower,{features,1}));
    model.push_back(h::reshape(sctx_,upper,{features,1}));
    const auto reg=scalar(ctx_->algo_param.lambda);
    const auto gamma=scalar(ctx_->algo_param.gamma);
    const auto zero=scalar(0.0f);
    const auto learning_rate=scalar(ctx_->algo_param.learning_rate);
    for(int t=0;t<ctx_->algo_param.tree_num;++t) {
        auto prob=ctx_->algo_param.objective==1?sigmoid(pred):pred;
        auto G=sub(prob,Y);
        auto H=ctx_->algo_param.objective==1?mul(prob,sub(ones,prob)):ones;
        std::function<spu::Value(const spu::Value&,int)> grow;
        grow=[&](const spu::Value&mask,int depth)->spu::Value {
            auto g=sum(mul(G,mask)); auto hv=sum(mul(H,mask));
            auto weight=mul(learning_rate,div(h::negate(sctx_,g),add(hv,reg)));
            model.push_back(weight);
            auto leaf=mul(mask,expand(weight));
            if(depth==ctx_->algo_param.max_depth) return leaf;
            auto parent=div(mul(g,g),add(hv,reg));
            auto best=h::seal(sctx_,scalar(-1e6f));
            auto selected=h::seal(sctx_,h::constant(sctx_,0.0f,spu::DT_F32,{n,1}));
            auto selected_id=h::seal(sctx_,scalar(-1.0f));
            for(size_t c=0;c<candidates.size();++c) {
                auto left=mul(mask,candidates[c]); auto right=sub(mask,left);
                auto gl=sum(mul(G,left)); auto hl=sum(mul(H,left));
                auto gr=sub(g,gl); auto hr=sub(hv,hl);
                auto gain=sub(mul(scalar(0.5f),sub(add(div(mul(gl,gl),add(hl,reg)),div(mul(gr,gr),add(hr,reg))),parent)),gamma);
                auto enough=h::bitwise_and(sctx_,h::greater_equal(sctx_,sum(left),scalar(1.0f)),h::greater_equal(sctx_,sum(right),scalar(1.0f)));
                auto better=h::bitwise_and(sctx_,enough,h::greater(sctx_,gain,best));
                best=h::select(sctx_,better,gain,best);
                selected=h::select(sctx_,h::broadcast_to(sctx_,better,{n,1}),left,selected);
                selected_id=h::select(sctx_,better,scalar(static_cast<float>(c)),selected_id);
            }
            auto split=h::greater(sctx_,best,zero);
            model.push_back(h::select(sctx_,split,selected_id,scalar(-1.0f)));
            auto left_prediction=grow(selected,depth+1);
            auto right_prediction=grow(sub(mask,selected),depth+1);
            auto children=add(left_prediction,right_prediction);
            return h::select(sctx_,h::broadcast_to(sctx_,split,{n,1}),children,leaf);
        };
        pred=add(pred,grow(ones,0));
        ic_impl::Audit("compute","round_complete",{{"round",t+1},{"total_rounds",ctx_->algo_param.tree_num},{"candidates",candidates.size()},{"depth",ctx_->algo_param.max_depth},{"intermediate_visibility","secret"}});
        SPDLOG_INFO("BFIA_COMPUTE algorithm=SS-XGB round={}/{} candidates={} depth={} intermediate_visibility=secret",
                    t+1,ctx_->algo_param.tree_num,candidates.size(),ctx_->algo_param.max_depth);
    }
    auto result=ctx_->algo_param.objective==1?sigmoid(pred):pred;
    auto path=ic_impl::util::GetOutputFileName("output/result.csv");
    ic_impl::WriteExactShares(sctx_,result,path);
    ic_impl::WriteExactShares(sctx_,h::concatenate(sctx_,model,0),path+".model.json");
    nlohmann::json metadata={{"format","bfia-secret-histogram-xgb-v1"},{"features",features},
      {"tree_count",ctx_->algo_param.tree_num},{"max_depth",ctx_->algo_param.max_depth},
      {"bins",bins},{"objective",ctx_->algo_param.objective},{"base_margin",base},
      {"layout","feature_min,feature_max,trees_preorder: node_weight,internal_candidate_id; -1 means leaf"},
      {"leaf_weight_includes_learning_rate",true}};
    std::ofstream meta_out(path+".model.metadata.json");
    YACL_ENFORCE(meta_out,"cannot open model metadata");
    meta_out << metadata.dump(2) << "\n";
    meta_out.flush();YACL_ENFORCE(meta_out.good(),"model metadata write failed");
}
} // namespace ic_impl::algo::ss_xgb
