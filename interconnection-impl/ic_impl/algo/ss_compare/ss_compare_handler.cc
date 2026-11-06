#include "ic_impl/numeric_output.h"
#include "ss_compare_handler.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <numeric>
#include "gflags/gflags.h"
#include "libspu/core/config.h"
#include "libspu/core/encoding.h"
#include "libspu/kernel/hal/constants.h"
#include "libspu/kernel/hal/polymorphic.h"
#include "libspu/kernel/hal/public_helper.h"
#include "libspu/kernel/hal/shape_ops.h"
#include "libspu/kernel/hal/type_cast.h"
#include "libspu/mpc/factory.h"
#include "libspu/mpc/semi2k/type.h"
#include "spdlog/spdlog.h"
#include "ic_impl/util.h"

#include "xtensor/xarray.hpp"
#include "xtensor/xadapt.hpp"

// gflags from LR re-used here
DECLARE_string(dataset);

namespace ic_impl::algo::ss_compare {

using org::interconnection::v2::ALGO_TYPE_SS_COMPARE;
using org::interconnection::v2::PROTOCOL_FAMILY_SS;
using org::interconnection::v2::protocol::PROTOCOL_KIND_ABY3;
using org::interconnection::v2::protocol::PROTOCOL_KIND_SEMI2K;
using org::interconnection::v2::protocol::SSProtocolProposal;
using org::interconnection::v2::protocol::SSProtocolResult;
using org::interconnection::v2::protocol::TRUNC_MODE_PROBABILISTIC;
using org::interconnection::v2::algos::SSCompareParamsProposal;
using org::interconnection::v2::algos::SSCompareParamsResult;
using org::interconnection::v2::algos::SSCompareDataIoProposal;
using org::interconnection::v2::algos::SSCompareDataIoResult;

namespace {

std::vector<std::set<int32_t>> CollectTruncModes(
    const std::vector<SSProtocolProposal>& ss_params) {
  std::vector<std::set<int32_t>> trunc_modes;
  for (const auto& ss_param : ss_params) {
    trunc_modes.resize(trunc_modes.size() + 1);
    for (const auto& item : ss_param.trunc_modes()) {
      trunc_modes.back().insert(item.method());
    }
  }
  return trunc_modes;
}

}  // namespace

bool SSCompareHandler::PrepareDataset() {
  // 读取本地 CSV，首行表头，后续逗号分隔
  headers_ = LoadHeader();
  X_local_ = LoadLocalCsv();
  YACL_ENFORCE(!X_local_.empty());
  // 设置 sample_size 与本方 feature_num
  ctx_->io_param.sample_size = static_cast<int64_t>(X_local_.size());
  auto self_rank = ctx_->ic_ctx->lctx->Rank();
  if (ctx_->io_param.feature_nums.size() < ctx_->ic_ctx->lctx->WorldSize()) {
    ctx_->io_param.feature_nums.resize(ctx_->ic_ctx->lctx->WorldSize());
  }
  ctx_->io_param.feature_nums[self_rank] =
      static_cast<int32_t>(X_local_[0].size());
  
  // 输出数据集尺寸信息
  int64_t rows = static_cast<int64_t>(X_local_.size());
  int64_t cols = X_local_.empty() ? 0 : static_cast<int64_t>(X_local_[0].size());
  SPDLOG_INFO("[SS-Compare] 数据集尺寸: {} 行 x {} 列 (样本数: {}, 特征数: {})", 
              rows, cols, rows, cols);
  
  return true;
}

std::vector<std::string> SSCompareHandler::LoadHeader() {
  std::ifstream file(ic_impl::util::GetInputFileName(::FLAGS_dataset));
  YACL_ENFORCE(file, "open file failed");
  std::string header_line;
  std::getline(file, header_line);
  std::vector<std::string> headers;
  std::stringstream ss(header_line);
  std::string item;
  while (std::getline(ss, item, ',')) headers.push_back(item);
  return headers;
}

std::vector<std::vector<float>> SSCompareHandler::LoadLocalCsv() {
  std::ifstream file(ic_impl::util::GetInputFileName(::FLAGS_dataset));
  YACL_ENFORCE(file, "open file failed");
  std::string line;
  std::getline(file, line);  // skip header
  std::vector<std::vector<float>> rows;
  while (std::getline(file, line)) {
    std::vector<float> row;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) {
      row.push_back(std::strtof(item.c_str(), nullptr));
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

bool SSCompareHandler::ProcessHandshakeResponse(const HandshakeResponseV2& resp) {
  if (!AlgoV2Handler::ProcessHandshakeResponse(resp)) return false;
  YACL_ENFORCE(resp.algo() == ALGO_TYPE_SS_COMPARE);
  
  SSCompareParamsResult algo_res;
  YACL_ENFORCE(resp.algo_param().UnpackTo(&algo_res));
  ctx_->compare_op = algo_res.compare_op();

  // ss 协议参数
  auto ss_param_optional = ic_impl::ExtractRspPfParam<SSProtocolResult>(resp, PROTOCOL_FAMILY_SS);
  YACL_ENFORCE(ss_param_optional.has_value());
  const auto& ss_param = ss_param_optional.value();
  ctx_->ss_param.fxp_bits = ss_param.fxp_fraction_bits();
  ctx_->ss_param.field_type = ss_param.field_type();
  ctx_->ss_param.protocol = ss_param.protocol();
  ctx_->ss_param.trunc_mode = ss_param.trunc_mode().method();
  ctx_->ss_param.shard_serialize_format = ss_param.shard_serialize_format();
  ctx_->ttp_config.ttp_server_host = ss_param.triple_config().server_host();
  ctx_->ttp_config.ttp_adjust_rank = ss_param.triple_config().adjust_rank();
  ctx_->ttp_config.ttp_server_version = ss_param.triple_config().version();

  // io 参数
  SSCompareDataIoResult io_res;
  YACL_ENFORCE(resp.io_param().UnpackTo(&io_res));
  ctx_->io_param.sample_size = io_res.sample_size();
  ctx_->io_param.feature_nums.assign(io_res.feature_nums().begin(), io_res.feature_nums().end());
  ctx_->io_param.result_to_rank = io_res.result_to_rank();
  ctx_->io_param.compare_column_idx = io_res.compare_column_idx();
  ctx_->io_param.compare_column_idx_b = io_res.compare_column_idx_b();
  return true;
}

HandshakeRequestV2 SSCompareHandler::BuildHandshakeRequest() {
  HandshakeRequestV2 req;
  auto self_rank = ctx_->ic_ctx->lctx->Rank();
  req.set_version(2);
  req.set_requester_rank(self_rank);
  req.add_supported_algos(ALGO_TYPE_SS_COMPARE);

  SSCompareParamsProposal algo_prop;
  algo_prop.add_supported_versions(1);
  algo_prop.add_compare_ops(0);  // 大于
  algo_prop.add_compare_ops(1);  // 小于
  algo_prop.add_compare_ops(2);  // 等于
  algo_prop.add_compare_ops(3);  // 大于等于
  algo_prop.add_compare_ops(4);  // 小于等于
  req.add_algo_params()->PackFrom(algo_prop);

  // SS 协议族
  req.add_protocol_families(PROTOCOL_FAMILY_SS);
  SSProtocolProposal ss_prop;
  ss_prop.add_supported_versions(1);
  ss_prop.add_supported_protocols(ctx_->ss_param.protocol);
  ss_prop.add_field_types(ctx_->ss_param.field_type);
  ss_prop.add_shard_serialize_formats(ctx_->ss_param.shard_serialize_format);
  auto* trunc_mode = ss_prop.add_trunc_modes();
  trunc_mode->add_supported_versions(1);
  trunc_mode->set_method(TRUNC_MODE_PROBABILISTIC);
  auto* prg = ss_prop.add_prg_configs();
  prg->add_supported_versions(1);
  prg->set_crypto_type(org::interconnection::v2::protocol::CRYPTO_TYPE_AES128_CTR);
  auto* ttp = ss_prop.add_triple_configs();
  ttp->add_supported_versions(1);
  ttp->set_sever_version(ctx_->ttp_config.ttp_server_version);
  req.add_protocol_family_params()->PackFrom(ss_prop);

  SSCompareDataIoProposal io_prop;
  io_prop.add_supported_versions(1);
  io_prop.set_sample_size(ctx_->io_param.sample_size);
  io_prop.set_feature_num(ctx_->io_param.feature_nums.at(self_rank));
  
  // 如果环境变量中设置了 compare_column_idx，计算本方的本地列索引
  // 环境变量中的 compare_column_idx 应该是全局列索引
  if (ctx_->io_param.compare_column_idx >= 0) {
    // 计算本方列索引（从全局列索引转换）
    int32_t global_col_idx = ctx_->io_param.compare_column_idx;
    int32_t col_offset = 0;
    for (int32_t i = 0; i < static_cast<int32_t>(self_rank); ++i) {
      col_offset += ctx_->io_param.feature_nums.at(i);
    }
    int32_t local_col_idx = global_col_idx - col_offset;
    // 如果全局列索引在本方的列范围内，使用本地列索引；否则使用0（默认）
    if (local_col_idx >= 0 && local_col_idx < ctx_->io_param.feature_nums.at(self_rank)) {
      io_prop.set_compare_column_idx(local_col_idx);
    } else {
      // 如果全局列索引不在本方范围内，使用0作为默认值
      // 实际协商时会使用第一个有效的列索引
      io_prop.set_compare_column_idx(0);
    }
  } else {
    // 如果没有设置，默认使用第一列
    io_prop.set_compare_column_idx(0);
  }
  
  // 设置 compare_column_idx_b（第二个列索引，用于两列比较）
  // 环境变量中的 compare_column_idx_b 应该是全局列索引
  if (ctx_->io_param.compare_column_idx_b >= 0) {
    // 计算本方列索引（从全局列索引转换）
    int32_t global_col_idx_b = ctx_->io_param.compare_column_idx_b;
    int32_t col_offset = 0;
    for (int32_t i = 0; i < static_cast<int32_t>(self_rank); ++i) {
      col_offset += ctx_->io_param.feature_nums.at(i);
    }
    int32_t local_col_idx_b = global_col_idx_b - col_offset;
    // 如果全局列索引在本方的列范围内，使用本地列索引；否则使用-1（表示不在本方范围内）
    if (local_col_idx_b >= 0 && local_col_idx_b < ctx_->io_param.feature_nums.at(self_rank)) {
      io_prop.set_compare_column_idx_b(local_col_idx_b);
    } else {
      // 如果全局列索引不在本方范围内，使用-1表示不在本方范围内
      // 实际协商时会使用全局列索引
      io_prop.set_compare_column_idx_b(-1);
    }
  } else {
    // 如果没有设置，使用-1表示不使用两列比较
    io_prop.set_compare_column_idx_b(-1);
  }
  
  req.mutable_io_param()->PackFrom(io_prop);
  return req;
}

HandshakeResponseV2 SSCompareHandler::BuildHandshakeResponse() {
  HandshakeResponseV2 resp;
  resp.mutable_header()->set_error_code(org::interconnection::OK);
  resp.set_algo(ALGO_TYPE_SS_COMPARE);

  SSCompareParamsResult algo_res;
  algo_res.set_version(1);
  algo_res.set_compare_op(ctx_->compare_op);
  resp.mutable_algo_param()->PackFrom(algo_res);

  resp.add_protocol_families(PROTOCOL_FAMILY_SS);
  SSProtocolResult ss_res;
  ss_res.set_protocol(ctx_->ss_param.protocol);
  ss_res.set_field_type(ctx_->ss_param.field_type);
  ss_res.set_fxp_fraction_bits(ctx_->ss_param.fxp_bits);
  ss_res.set_shard_serialize_format(ctx_->ss_param.shard_serialize_format);
  ss_res.mutable_trunc_mode()->set_version(1);
  ss_res.mutable_trunc_mode()->set_method(ctx_->ss_param.trunc_mode);
  ss_res.mutable_triple_config()->set_version(1);
  ss_res.mutable_triple_config()->set_server_host(ctx_->ttp_config.ttp_server_host);
  ss_res.mutable_triple_config()->set_adjust_rank(ctx_->ttp_config.ttp_adjust_rank);
  resp.add_protocol_family_params()->PackFrom(ss_res);

  SSCompareDataIoResult io_res;
  io_res.set_version(1);
  io_res.set_sample_size(ctx_->io_param.sample_size);
  io_res.mutable_feature_nums()->Add(ctx_->io_param.feature_nums.begin(), ctx_->io_param.feature_nums.end());
  io_res.set_result_to_rank(ctx_->io_param.result_to_rank);
  io_res.set_compare_column_idx(ctx_->io_param.compare_column_idx);
  io_res.set_compare_column_idx_b(ctx_->io_param.compare_column_idx_b);
  resp.mutable_io_param()->PackFrom(io_res);
  return resp;
}

status::ErrorStatus SSCompareHandler::NegotiateHandshakeParams(
    const std::vector<HandshakeRequestV2>& requests) {
  // 协商 SS 协议族
  auto ss_params = ic_impl::ExtractReqPfParams<SSProtocolProposal>(requests, PROTOCOL_FAMILY_SS);
  if (ss_params.empty()) return status::InvalidRequestError("no ss params");
  // 协议/字段/截断/PRG/TTP 的交集检查，简化实现，仅检查截断包含 probabilistic
  auto trunc_modes = CollectTruncModes(ss_params);
  bool ok = false;
  for (const auto& s : trunc_modes) {
    if (s.count(TRUNC_MODE_PROBABILISTIC)) {
      ok = true;
      break;
    }
  }
  if (!ok) return status::UnsupportedArgumentError("trunc mode unsupported");

  // 协商比较操作类型
  std::vector<int32_t> common_ops;
  bool first = true;
  for (const auto& req : requests) {
    SSCompareParamsProposal algo;
    if (!req.algo_params().empty() && req.algo_params(0).UnpackTo(&algo)) {
      if (first) {
        common_ops.assign(algo.compare_ops().begin(), algo.compare_ops().end());
        first = false;
      } else {
        std::vector<int32_t> intersection;
        std::set_intersection(
            common_ops.begin(), common_ops.end(),
            algo.compare_ops().begin(), algo.compare_ops().end(),
            std::back_inserter(intersection));
        common_ops = intersection;
      }
    }
  }
  if (common_ops.empty()) {
    return status::HandshakeRefusedError("no common compare ops");
  }
  ctx_->compare_op = common_ops[0];  // 使用第一个共同支持的操作

  // 协商 IO 和列索引
  int32_t global_col_idx = -1;
  for (const auto& req : requests) {
    SSCompareDataIoProposal io;
    if (!req.io_param().UnpackTo(&io)) {
      return status::InvalidRequestError("invalid io param");
    }
    if (io.sample_size() <= 0 || io.feature_num() <= 0) {
      return status::InvalidRequestError("invalid sample_size/feature_num");
    }
    if (ctx_->io_param.sample_size == 0) {
      ctx_->io_param.sample_size = io.sample_size();
    } else if (ctx_->io_param.sample_size != io.sample_size()) {
      return status::HandshakeRefusedError("sample size inconsistent");
    }
    if (static_cast<int32_t>(ctx_->io_param.feature_nums.size()) <= req.requester_rank()) {
      ctx_->io_param.feature_nums.resize(ctx_->ic_ctx->lctx->WorldSize());
    }
    ctx_->io_param.feature_nums[req.requester_rank()] = io.feature_num();
    
    // 转换各方本地列索引到全局列索引
    // 如果有多个请求都指定了列索引，使用第一个有效的
    if (io.compare_column_idx() >= 0) {
      int32_t col_offset = 0;
      for (int32_t i = 0; i < req.requester_rank(); ++i) {
        col_offset += ctx_->io_param.feature_nums.at(i);
      }
      int32_t candidate_global_col_idx = col_offset + io.compare_column_idx();
      // 验证列索引是否有效
      int32_t total_cols = 0;
      for (auto fn : ctx_->io_param.feature_nums) {
        total_cols += fn;
      }
      if (candidate_global_col_idx >= 0 && candidate_global_col_idx < total_cols) {
        if (global_col_idx < 0) {
          global_col_idx = candidate_global_col_idx;
        } else if (global_col_idx != candidate_global_col_idx) {
          // 如果各方指定的列索引不一致，使用第一个有效的
          SPDLOG_WARN("Inconsistent compare_column_idx from different parties, using first valid one: {}", global_col_idx);
        }
      }
    }
  }
  
  // 如果没有从请求中获取到有效的列索引，检查环境变量
  if (global_col_idx < 0) {
    global_col_idx = ctx_->io_param.compare_column_idx;
  }
  
  // 如果还是没有，默认使用第一列（列索引0）
  if (global_col_idx < 0) {
    global_col_idx = 0;
  }
  
  ctx_->io_param.compare_column_idx = global_col_idx;
  
  // 协商第二个列索引 compare_column_idx_b（用于两列比较）
  int32_t global_col_idx_b = -1;
  for (const auto& req : requests) {
    SSCompareDataIoProposal io;
    if (!req.io_param().UnpackTo(&io)) {
      continue;
    }
    // 如果请求中指定了 compare_column_idx_b
    if (io.compare_column_idx_b() >= 0) {
      int32_t col_offset = 0;
      for (int32_t i = 0; i < req.requester_rank(); ++i) {
        col_offset += ctx_->io_param.feature_nums.at(i);
      }
      int32_t candidate_global_col_idx_b = col_offset + io.compare_column_idx_b();
      // 验证列索引是否有效
      int32_t total_cols = 0;
      for (auto fn : ctx_->io_param.feature_nums) {
        total_cols += fn;
      }
      if (candidate_global_col_idx_b >= 0 && candidate_global_col_idx_b < total_cols) {
        if (global_col_idx_b < 0) {
          global_col_idx_b = candidate_global_col_idx_b;
        } else if (global_col_idx_b != candidate_global_col_idx_b) {
          // 如果各方指定的列索引不一致，使用第一个有效的
          SPDLOG_WARN("Inconsistent compare_column_idx_b from different parties, using first valid one: {}", global_col_idx_b);
        }
      }
    }
  }
  
  // 如果没有从请求中获取到有效的列索引，检查环境变量
  if (global_col_idx_b < 0) {
    global_col_idx_b = ctx_->io_param.compare_column_idx_b;
  }
  
  ctx_->io_param.compare_column_idx_b = global_col_idx_b;
  
  // 验证列索引是否有效
  int32_t total_cols = 0;
  for (auto fn : ctx_->io_param.feature_nums) {
    total_cols += fn;
  }
  if (ctx_->io_param.compare_column_idx >= total_cols) {
    return status::InvalidRequestError("compare_column_idx out of range");
  }
  if (ctx_->io_param.compare_column_idx_b >= 0 && ctx_->io_param.compare_column_idx_b >= total_cols) {
    return status::InvalidRequestError("compare_column_idx_b out of range");
  }
  if (ctx_->io_param.compare_column_idx_b >= 0 && ctx_->io_param.compare_column_idx == ctx_->io_param.compare_column_idx_b) {
    return status::InvalidRequestError("compare_column_idx and compare_column_idx_b must be different");
  }
  
  return status::OkStatus();
}

std::unique_ptr<spu::SPUContext> SSCompareHandler::MakeSpuContext() {
  static const std::map<int32_t, spu::ProtocolKind> protocol_map{
      {PROTOCOL_KIND_SEMI2K, spu::ProtocolKind::SEMI2K},
      {PROTOCOL_KIND_ABY3, spu::ProtocolKind::ABY3}};
  auto lctx = ctx_->ic_ctx->lctx;
  spu::RuntimeConfig config;
  config.set_protocol(protocol_map.at(ctx_->ss_param.protocol));
  config.set_field(static_cast<spu::FieldType>(ctx_->ss_param.field_type));
  config.set_fxp_fraction_bits(ctx_->ss_param.fxp_bits);
  if (ctx_->ss_param.trunc_mode == TRUNC_MODE_PROBABILISTIC) {
    config.set_trunc_allow_msb_error(true);
  }
  populateRuntimeConfig(config);
  if (ctx_->ttp_config.use_ttp) {
    config.set_beaver_type(spu::RuntimeConfig_BeaverType_TrustedThirdParty);
    config.mutable_ttp_beaver_config()->set_server_host(ctx_->ttp_config.ttp_server_host);
    config.mutable_ttp_beaver_config()->set_adjust_rank(ctx_->ttp_config.ttp_adjust_rank);
  } else {
    config.set_beaver_type(spu::RuntimeConfig_BeaverType_TrustedFirstParty);
  }
  return std::make_unique<spu::SPUContext>(config, lctx);
}

spu::Value SSCompareHandler::EncodeToShare(const std::vector<std::vector<float>>& X) {
  const int64_t rows = static_cast<int64_t>(X.size());
  const int64_t cols = static_cast<int64_t>(X.empty() ? 0 : X[0].size());
  std::vector<float> flat;
  flat.reserve(rows * cols);
  for (const auto& row : X) {
    flat.insert(flat.end(), row.begin(), row.end());
  }
  // 使用 xtensor 适配到 PtBufferView
  auto arr = xt::adapt(flat, std::vector<size_t>{static_cast<size_t>(rows), static_cast<size_t>(cols)});
  spu::PtBufferView pv(arr);
  spu::DataType dtype;
  auto encoded = encodeToRing(pv, static_cast<spu::FieldType>(ctx_->ss_param.field_type), ctx_->ss_param.fxp_bits, &dtype);
  return spu::Value(encoded, dtype);
}

void SSCompareHandler::RunAlgo() {
  auto sctx_holder = MakeSpuContext();
  sctx_ = sctx_holder.get();
  spu::mpc::Factory::RegisterProtocol(sctx_, sctx_->lctx());

  // 将各方数据编码为秘密分享
  std::size_t self_rank = ctx_->ic_ctx->lctx->Rank();
  std::size_t world = ctx_->ic_ctx->lctx->WorldSize();
  auto x_local = EncodeToShare(X_local_);
  // 设置存储类型
  x_local.storage_type() = spu::makeType<spu::mpc::semi2k::AShrTy>(static_cast<spu::FieldType>(ctx_->ss_param.field_type));
  
  // 收集所有参与方的数据分享
  std::vector<spu::Value> x_parts(world);
  x_parts[self_rank] = std::move(x_local);
  
  // 对其他参与方的数据创建零分享（在实际应用中，这些会通过SPU的infeed机制获取）
  for (std::size_t i = 0; i < world; ++i) {
    if (i == self_rank) continue;
    int64_t rows = ctx_->io_param.sample_size;
    int64_t cols = ctx_->io_param.feature_nums.at(i);
    auto z = spu::kernel::hal::zeros(sctx_, spu::DT_F32, {rows, cols});
    z.storage_type() = spu::makeType<spu::mpc::semi2k::AShrTy>(static_cast<spu::FieldType>(ctx_->ss_param.field_type));
    x_parts[i] = std::move(z);
  }
  
  // 将各方的数据横向拼接（按特征维度，形成完整的特征矩阵）
  auto X = spu::kernel::hal::concatenate(sctx_, x_parts, 1);
  
  // 提取要比较的列
  YACL_ENFORCE(ctx_->io_param.compare_column_idx >= 0, 
               "compare_column_idx must be set");
  int32_t col_idx_a = ctx_->io_param.compare_column_idx;
  int32_t col_idx_b = ctx_->io_param.compare_column_idx_b;
  
  // 提取第一列（列A）：[sample_size, 1]
  auto target_col_a = spu::kernel::hal::slice(
      sctx_, X, 
      {0, static_cast<int64_t>(col_idx_a)}, 
      {ctx_->io_param.sample_size, static_cast<int64_t>(col_idx_a) + 1}, 
      {});
  
  spu::Value result;
  spu::Value target_col;
  spu::Value threshold;
  
  // 判断是两列比较还是单列与均值比较
  if (col_idx_b >= 0) {
    // 两列比较模式：Alice 的某列 vs Bob 的某列
    // 提取第二列（列B）：[sample_size, 1]
    auto target_col_b = spu::kernel::hal::slice(
        sctx_, X, 
        {0, static_cast<int64_t>(col_idx_b)}, 
        {ctx_->io_param.sample_size, static_cast<int64_t>(col_idx_b) + 1}, 
        {});
    
    target_col = target_col_a;
    threshold = target_col_b;
    
    SPDLOG_INFO("[SS-Compare] Two-column comparison: column {} vs column {} (global indices)", col_idx_a, col_idx_b);
    SPDLOG_INFO("[SS-Compare] Compare operation: {} (0=greater, 1=less, 2=equal, 3=greater_equal, 4=less_equal)", ctx_->compare_op);
  } else {
    // 单列与均值比较模式：列A vs 列A的均值
    target_col = target_col_a;
    
    // 计算该列的均值作为阈值
    // 这个阈值是基于拼接后的全局数据计算的，包含了所有参与方的数据
    auto ones = spu::kernel::hal::broadcast_to(
        sctx_, 
        spu::kernel::hal::constant(sctx_, 1.0F, spu::DT_F32), 
        {ctx_->io_param.sample_size, 1});
    auto col_sum = spu::kernel::hal::matmul(
        sctx_, 
        spu::kernel::hal::transpose(sctx_, target_col_a), 
        ones);
    // col_sum 的形状是 [1, 1]
    auto n_val = spu::kernel::hal::constant(
        sctx_, 
        static_cast<float>(ctx_->io_param.sample_size), 
        spu::DT_F32);
    auto inv_n = spu::kernel::hal::reciprocal(sctx_, n_val);
    // 将 inv_n 广播到 col_sum 的形状 [1, 1]
    auto inv_n_b = spu::kernel::hal::broadcast_to(sctx_, inv_n, col_sum.shape());
    auto mean_val = spu::kernel::hal::mul(sctx_, col_sum, inv_n_b);
    // mean_val 的形状是 [1, 1]，需要广播到 target_col 的形状 [sample_size, 1]
    threshold = spu::kernel::hal::broadcast_to(
        sctx_, 
        mean_val, 
        target_col_a.shape());
    
    SPDLOG_INFO("[SS-Compare] Single-column comparison: column {} (global index) against its mean value", col_idx_a);
    SPDLOG_INFO("[SS-Compare] Compare operation: {} (0=greater, 1=less, 2=equal, 3=greater_equal, 4=less_equal)", ctx_->compare_op);
  }
  
  // 根据比较操作类型执行比较
  // 使用less操作和逻辑组合实现所有比较操作
  
  spu::Value result_f32;
  
  if (ctx_->compare_op == 0) {
    // 大于: target_col > threshold 等价于 threshold < target_col
    // 即：列A > 列B（两列比较）或 列A > 均值（单列比较）
    auto less_bool = spu::kernel::hal::less(sctx_, threshold, target_col);
    auto less_f32 = spu::kernel::hal::dtype_cast(sctx_, less_bool, spu::DT_F32);
    // 使用算术运算确保是纯 F32 类型：1.0 * less_f32
    auto one = spu::kernel::hal::constant(sctx_, 1.0f, spu::DT_F32, less_f32.shape());
    result_f32 = spu::kernel::hal::mul(sctx_, one, less_f32);
  } else if (ctx_->compare_op == 1) {
    // 小于: target_col < threshold
    // 即：列A < 列B（两列比较）或 列A < 均值（单列比较）
    auto less_bool = spu::kernel::hal::less(sctx_, target_col, threshold);
    auto less_f32 = spu::kernel::hal::dtype_cast(sctx_, less_bool, spu::DT_F32);
    auto one = spu::kernel::hal::constant(sctx_, 1.0f, spu::DT_F32, less_f32.shape());
    result_f32 = spu::kernel::hal::mul(sctx_, one, less_f32);
  } else if (ctx_->compare_op == 2) {
    // 等于: target_col == threshold (使用绝对值小于epsilon)
    // 即：列A == 列B（两列比较）或 列A == 均值（单列比较）
    auto diff = spu::kernel::hal::sub(sctx_, target_col, threshold);
    auto abs_diff = spu::kernel::hal::abs(sctx_, diff);
    auto eps = spu::kernel::hal::constant(sctx_, 1e-6f, spu::DT_F32, {1, 1});
    auto eps_broadcast = spu::kernel::hal::broadcast_to(sctx_, eps, abs_diff.shape());
    auto less_bool = spu::kernel::hal::less(sctx_, abs_diff, eps_broadcast);
    auto less_f32 = spu::kernel::hal::dtype_cast(sctx_, less_bool, spu::DT_F32);
    auto one = spu::kernel::hal::constant(sctx_, 1.0f, spu::DT_F32, less_f32.shape());
    result_f32 = spu::kernel::hal::mul(sctx_, one, less_f32);
  } else if (ctx_->compare_op == 3) {
    // 大于等于: target_col >= threshold 等价于 not (target_col < threshold)
    // 即：列A >= 列B（两列比较）或 列A >= 均值（单列比较）
    auto less_bool = spu::kernel::hal::less(sctx_, target_col, threshold);
    auto less_f32 = spu::kernel::hal::dtype_cast(sctx_, less_bool, spu::DT_F32);
    auto one = spu::kernel::hal::constant(sctx_, 1.0f, spu::DT_F32, less_f32.shape());
    result_f32 = spu::kernel::hal::sub(sctx_, one, less_f32);
  } else if (ctx_->compare_op == 4) {
    // 小于等于: target_col <= threshold 等价于 not (threshold < target_col)
    // 即：列A <= 列B（两列比较）或 列A <= 均值（单列比较）
    auto greater_bool = spu::kernel::hal::less(sctx_, threshold, target_col);
    auto greater_f32 = spu::kernel::hal::dtype_cast(sctx_, greater_bool, spu::DT_F32);
    auto one = spu::kernel::hal::constant(sctx_, 1.0f, spu::DT_F32, greater_f32.shape());
    result_f32 = spu::kernel::hal::sub(sctx_, one, greater_f32);
  } else {
    YACL_THROW("unsupported compare_op: {}", ctx_->compare_op);
  }

  // 在 reveal 之前，对结果进行一次算术运算，确保值是纯 F32 类型
  // 这可以帮助清除任何布尔类型的内部表示
  auto zero = spu::kernel::hal::constant(sctx_, 0.0f, spu::DT_F32, result_f32.shape());
  auto result_clean = spu::kernel::hal::add(sctx_, result_f32, zero);
  
  // 将清理后的结果赋值给 result（用于后续的 reveal）
  result = result_clean;

  // 按握手协商的结果接收方输出
  SaveResultToOutput(sctx_, result);
}

void SSCompareHandler::SaveResultToOutput(spu::SPUContext* sctx, const spu::Value& result) {
  const auto path=ic_impl::util::GetOutputFileName("output/result.csv");
  ic_impl::WriteRecipientResult(sctx,result,path,ctx_->io_param.result_to_rank,true);
}

}  // namespace ic_impl::algo::ss_compare

