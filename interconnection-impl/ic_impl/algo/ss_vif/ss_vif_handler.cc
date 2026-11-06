#include "ic_impl/numeric_output.h"
#include "ss_vif_handler.h"
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
#include "libspu/mpc/factory.h"
#include "libspu/mpc/semi2k/type.h"
#include "spdlog/spdlog.h"
#include "ic_impl/util.h"

#include "xtensor/xarray.hpp"
#include "xtensor/xadapt.hpp"

// gflags from LR re-used here
DECLARE_string(dataset);

namespace ic_impl::algo::ss_vif {

using org::interconnection::v2::PROTOCOL_FAMILY_SS;
using org::interconnection::v2::ALGO_TYPE_SS_VIF;
using org::interconnection::v2::protocol::PROTOCOL_KIND_ABY3;
using org::interconnection::v2::protocol::PROTOCOL_KIND_SEMI2K;
using org::interconnection::v2::protocol::SSProtocolProposal;
using org::interconnection::v2::protocol::SSProtocolResult;
using org::interconnection::v2::protocol::TRUNC_MODE_PROBABILISTIC;
using org::interconnection::v2::algos::SSVifParamsProposal;
using org::interconnection::v2::algos::SSVifParamsResult;
using org::interconnection::v2::algos::SSVifDataIoProposal;
using org::interconnection::v2::algos::SSVifDataIoResult;

namespace {

}  // namespace

bool SsVifHandler::PrepareDataset() {
  headers_ = LoadHeader();
  X_local_ = LoadLocalCsv();
  YACL_ENFORCE(!X_local_.empty());
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
  SPDLOG_INFO("[SS-VIF] 数据集尺寸: {} 行 x {} 列 (样本数: {}, 特征数: {})", 
              rows, cols, rows, cols);
  
  return true;
}

std::vector<std::string> SsVifHandler::LoadHeader() {
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

std::vector<std::vector<float>> SsVifHandler::LoadLocalCsv() {
  std::ifstream file(ic_impl::util::GetInputFileName(::FLAGS_dataset));
  YACL_ENFORCE(file, "open file failed");
  std::string line;
  std::getline(file, line);  // skip header
  std::unordered_map<std::string, std::size_t> header_idx;
  for (std::size_t i = 0; i < headers_.size(); ++i) header_idx[headers_[i]] = i;
  std::vector<std::size_t> col_indices;
  auto self_rank = ctx_->ic_ctx->lctx->Rank();
  auto self_party_id = ctx_->ic_ctx->lctx->PartyIdByRank(self_rank);
  const auto& fs = ctx_->io_param.feature_select;
  if (!fs.empty() && fs.count(self_party_id)) {
    col_indices.reserve(fs.at(self_party_id).size());
    for (const auto& name : fs.at(self_party_id)) {
      YACL_ENFORCE(header_idx.count(name) > 0, "feature {} not found", name);
      col_indices.push_back(header_idx[name]);
    }
  } else {
    col_indices.resize(headers_.size());
    std::iota(col_indices.begin(), col_indices.end(), 0);
  }
  std::vector<std::vector<float>> rows;
  while (std::getline(file, line)) {
    std::vector<float> row;
    std::stringstream ss(line);
    std::string item;
    std::vector<float> all;
    while (std::getline(ss, item, ',')) {
      all.push_back(std::strtof(item.c_str(), nullptr));
    }
    row.reserve(col_indices.size());
    for (auto idx : col_indices) row.push_back(all.at(idx));
    rows.push_back(std::move(row));
  }
  return rows;
}

bool SsVifHandler::ProcessHandshakeResponse(const HandshakeResponseV2& resp) {
  if (!AlgoV2Handler::ProcessHandshakeResponse(resp)) return false;
  YACL_ENFORCE(resp.algo() == ALGO_TYPE_SS_VIF);
  SSVifParamsResult algo_res;
  YACL_ENFORCE(resp.algo_param().UnpackTo(&algo_res));

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

  SSVifDataIoResult io_res;
  YACL_ENFORCE(resp.io_param().UnpackTo(&io_res));
  ctx_->io_param.sample_size = io_res.sample_size();
  ctx_->io_param.feature_nums.assign(io_res.feature_nums().begin(), io_res.feature_nums().end());
  ctx_->io_param.result_to_rank = io_res.result_to_rank();
  return true;
}

HandshakeRequestV2 SsVifHandler::BuildHandshakeRequest() {
  HandshakeRequestV2 req;
  auto self_rank = ctx_->ic_ctx->lctx->Rank();
  req.set_version(2);
  req.set_requester_rank(self_rank);
  req.add_supported_algos(ALGO_TYPE_SS_VIF);

  SSVifParamsProposal algo_prop; algo_prop.add_supported_versions(1);
  req.add_algo_params()->PackFrom(algo_prop);

  req.add_protocol_families(PROTOCOL_FAMILY_SS);
  SSProtocolProposal ss_prop; ss_prop.add_supported_versions(1);
  ss_prop.add_supported_protocols(ctx_->ss_param.protocol);
  ss_prop.add_field_types(ctx_->ss_param.field_type);
  ss_prop.add_shard_serialize_formats(ctx_->ss_param.shard_serialize_format);
  auto* trunc_mode = ss_prop.add_trunc_modes();
  trunc_mode->add_supported_versions(1);
  trunc_mode->set_method(TRUNC_MODE_PROBABILISTIC);
  req.add_protocol_family_params()->PackFrom(ss_prop);
  SSVifDataIoProposal io_prop; io_prop.add_supported_versions(1);
  io_prop.set_sample_size(ctx_->io_param.sample_size);
  io_prop.set_feature_num(ctx_->io_param.feature_nums.at(self_rank));
  req.mutable_io_param()->PackFrom(io_prop);
  return req;
}

HandshakeResponseV2 SsVifHandler::BuildHandshakeResponse() {
  HandshakeResponseV2 resp;
  resp.mutable_header()->set_error_code(org::interconnection::OK);
  resp.set_algo(ALGO_TYPE_SS_VIF);

  SSVifParamsResult algo_res; algo_res.set_version(1);
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
  SSVifDataIoResult io_res; io_res.set_version(1);
  io_res.set_sample_size(ctx_->io_param.sample_size);
  io_res.mutable_feature_nums()->Add(ctx_->io_param.feature_nums.begin(), ctx_->io_param.feature_nums.end());
  io_res.set_result_to_rank(ctx_->io_param.result_to_rank);
  resp.mutable_io_param()->PackFrom(io_res);
  return resp;
}

status::ErrorStatus SsVifHandler::NegotiateHandshakeParams(
    const std::vector<HandshakeRequestV2>& requests) {
  auto ss_params = ic_impl::ExtractReqPfParams<SSProtocolProposal>(requests, PROTOCOL_FAMILY_SS);
  if (ss_params.empty()) return status::InvalidRequestError("no ss params");
  // 简单校验截断模式
  bool ok = false;
  for (const auto& req : requests) {
    (void)req; ok = true; // 简化：只要提交了 SS 协议即可
  }
  if (!ok) return status::UnsupportedArgumentError("trunc mode unsupported");

  for (const auto& req : requests) {
    SSVifDataIoProposal io;
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
  }
  return status::OkStatus();
}

std::unique_ptr<spu::SPUContext> SsVifHandler::MakeSpuContext() {
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

spu::Value SsVifHandler::EncodeToShare(const std::vector<std::vector<float>>& X) {
  const int64_t rows = static_cast<int64_t>(X.size());
  const int64_t cols = static_cast<int64_t>(X.empty() ? 0 : X[0].size());
  std::vector<float> flat; flat.reserve(rows * cols);
  for (const auto& row : X) { flat.insert(flat.end(), row.begin(), row.end()); }
  auto arr = xt::adapt(flat, std::vector<size_t>{static_cast<size_t>(rows), static_cast<size_t>(cols)});
  spu::PtBufferView pv(arr);
  spu::DataType dtype;
  auto encoded = encodeToRing(pv, static_cast<spu::FieldType>(ctx_->ss_param.field_type), ctx_->ss_param.fxp_bits, &dtype);
  return spu::Value(encoded, dtype);
}

void SsVifHandler::RunAlgo() {
  auto sctx = MakeSpuContext();
  spu::mpc::Factory::RegisterProtocol(sctx.get(), sctx->lctx());

  std::size_t self_rank = ctx_->ic_ctx->lctx->Rank();
  std::size_t world = ctx_->ic_ctx->lctx->WorldSize();
  auto x_local = EncodeToShare(X_local_);
  x_local.storage_type() = spu::makeType<spu::mpc::semi2k::AShrTy>(static_cast<spu::FieldType>(ctx_->ss_param.field_type));
  std::vector<spu::Value> x_parts(world);
  x_parts[self_rank] = std::move(x_local);
  for (std::size_t i = 0; i < world; ++i) {
    if (i == self_rank) continue;
    int64_t rows = ctx_->io_param.sample_size;
    int64_t cols = ctx_->io_param.feature_nums.at(i);
    auto z = spu::kernel::hal::zeros(sctx.get(), spu::DT_F32, {rows, cols});
    z.storage_type() = spu::makeType<spu::mpc::semi2k::AShrTy>(static_cast<spu::FieldType>(ctx_->ss_param.field_type));
    x_parts[i] = std::move(z);
  }
  auto X = spu::kernel::hal::concatenate(sctx.get(), x_parts, 1);

  // 标准化
  auto ones = spu::kernel::hal::broadcast_to(sctx.get(), spu::kernel::hal::constant(sctx.get(), 1.0F, spu::DT_F32), {ctx_->io_param.sample_size, 1});
  auto X_sum = spu::kernel::hal::matmul(sctx.get(), spu::kernel::hal::transpose(sctx.get(), X), ones);
  auto n_val = spu::kernel::hal::constant(sctx.get(), static_cast<float>(ctx_->io_param.sample_size), spu::DT_F32);
  auto inv_n = spu::kernel::hal::reciprocal(sctx.get(), n_val);
  auto inv_n_b = spu::kernel::hal::broadcast_to(sctx.get(), inv_n, X_sum.shape());
  auto mean = spu::kernel::hal::mul(sctx.get(), X_sum, inv_n_b);
  auto mean_T = spu::kernel::hal::transpose(sctx.get(), mean);
  auto mean_row = spu::kernel::hal::matmul(sctx.get(), ones, mean_T);
  auto Xc = spu::kernel::hal::sub(sctx.get(), X, mean_row);

  // 方差与标准差
  auto Xc2 = spu::kernel::hal::mul(sctx.get(), Xc, Xc);
  auto var_num = spu::kernel::hal::matmul(sctx.get(), spu::kernel::hal::transpose(sctx.get(), Xc2), ones);
  auto inv_denom_var = spu::kernel::hal::broadcast_to(sctx.get(), inv_n, var_num.shape());
  auto var = spu::kernel::hal::mul(sctx.get(), var_num, inv_denom_var); // (d,1)
  auto eps = spu::kernel::hal::constant(sctx.get(), 1e-6f, spu::DT_F32, {1,1});
  auto var_safe = spu::kernel::hal::add(sctx.get(), var, spu::kernel::hal::broadcast_to(sctx.get(), eps, var.shape()));
  auto y = spu::kernel::hal::rsqrt(sctx.get(), var_safe);
  auto inv_std = y; // (d,1)
  // 将 inv_std (d,1) 转置为 (1,d)，然后广播到 (n,d) 来对 Xc 的每一列进行缩放
  auto inv_std_row = spu::kernel::hal::transpose(sctx.get(), inv_std); // (1,d)
  auto inv_std_broadcast = spu::kernel::hal::broadcast_to(sctx.get(), inv_std_row, Xc.shape()); // (n,d)
  auto Xs = spu::kernel::hal::mul(sctx.get(), Xc, inv_std_broadcast); // 每列乘以 inv_std（即除以std）

  // 总体方差标准化，因此 X^T X = sample_size * corr。
  auto xtx = spu::kernel::hal::matmul(sctx.get(), spu::kernel::hal::transpose(sctx.get(), Xs), Xs);

  // 在 SPU 中使用牛顿迭代计算 xtx 的逆矩阵
  // 算法：a = (1/trace(xtx)) * I, 然后迭代 a = a * (2*I - xtx*a)
  int64_t n = xtx.shape()[0];
  YACL_ENFORCE(n == xtx.shape()[1], "xtx must be square matrix");
  
  // 计算 trace(xtx) = sum(diag(xtx))
  // 创建单位矩阵掩码（对角线为1，其他为0）用于提取对角线元素
  std::vector<float> eye_mask_data(n * n, 0.0f);
  for (int64_t i = 0; i < n; ++i) {
    eye_mask_data[i * n + i] = 1.0f;
  }
  // This is a public identity matrix, not an identical share at every rank.
  auto eye_mask = spu::kernel::hal::constant(sctx.get(), eye_mask_data, spu::DT_F32, {n,n});
  // 计算 trace(xtx) = sum(diag(xtx))
  // 方法：xtx * eye_mask 得到只有对角线非零的矩阵，然后使用矩阵乘法求和
  auto diag_mask_mul = spu::kernel::hal::mul(sctx.get(), xtx, eye_mask);
  // 创建全 1 的列向量用于求和
  auto ones_col = spu::kernel::hal::broadcast_to(sctx.get(), spu::kernel::hal::constant(sctx.get(), 1.0F, spu::DT_F32), {n, 1});
  // 对每行求和：diag_vec = diag_mask_mul * ones_col，得到 (n, 1)
  auto diag_vec = spu::kernel::hal::matmul(sctx.get(), diag_mask_mul, ones_col);
  // 创建全 1 的行向量用于计算 trace
  auto ones_row = spu::kernel::hal::broadcast_to(sctx.get(), spu::kernel::hal::constant(sctx.get(), 1.0F, spu::DT_F32), {1, n});
  // trace = ones_row * diag_vec，得到 (1, 1) 矩阵
  auto trace_mat = spu::kernel::hal::matmul(sctx.get(), ones_row, diag_vec);
  // trace_mat 是 (1, 1)，可以直接用作标量进行 reciprocal 和 broadcast
  auto inv_trace = spu::kernel::hal::reciprocal(sctx.get(), trace_mat);
  
  // 初始化：a = (1/trace) * I，I 就是 eye_mask
  // 将 inv_trace (1,1) 广播到 eye_mask 的形状 (n, n)
  auto inv_trace_broadcast = spu::kernel::hal::broadcast_to(sctx.get(), inv_trace, eye_mask.shape());
  auto a = spu::kernel::hal::mul(sctx.get(), eye_mask, inv_trace_broadcast);
  
  // 牛顿迭代：a = a * (2*I - xtx*a)，默认 20 次迭代
  int iter_round = ic_impl::util::GetParamEnv("vif_newton_iter", 20);
  if (iter_round < 1) iter_round = 20;
  // 创建 2*I：先创建标量 2，然后广播到 eye_mask 的形状
  auto two_scalar = spu::kernel::hal::constant(sctx.get(), 2.0F, spu::DT_F32);
  auto two_broadcast = spu::kernel::hal::broadcast_to(sctx.get(), two_scalar, eye_mask.shape());
  auto two_I = spu::kernel::hal::mul(sctx.get(), eye_mask, two_broadcast);
  
  for (int iter = 0; iter < iter_round; ++iter) {
    auto xtx_a = spu::kernel::hal::matmul(sctx.get(), xtx, a);
    auto two_I_minus_xtx_a = spu::kernel::hal::sub(sctx.get(), two_I, xtx_a);
    a = spu::kernel::hal::matmul(sctx.get(), a, two_I_minus_xtx_a);
    SPDLOG_INFO("BFIA_COMPUTE algorithm=SS-VIF stage=matrix_inverse iteration={}/{} dimensions={} intermediate_visibility=secret",iter+1,iter_round,n);
  }
  
  // 提取对角线：diag(inv(xtx))
  // 方法：a * eye_mask 得到只有对角线非零的矩阵，然后使用矩阵乘法提取对角线
  auto diag_a_mul = spu::kernel::hal::mul(sctx.get(), a, eye_mask);
  // 使用全 1 列向量对每行求和，得到对角线元素向量 (n, 1)
  auto diag_a_vec = spu::kernel::hal::matmul(sctx.get(), diag_a_mul, ones_col);
  
  // VIF = sample_size * diag(inv(xtx)) = diag(inv(corr))
  auto sample_count = spu::kernel::hal::constant(sctx.get(), static_cast<float>(ctx_->io_param.sample_size), spu::DT_F32);
  auto sample_count_broadcast = spu::kernel::hal::broadcast_to(sctx.get(), sample_count, diag_a_vec.shape());
  auto vif_shared = spu::kernel::hal::mul(sctx.get(), diag_a_vec, sample_count_broadcast);

  const auto output=ic_impl::util::GetOutputFileName("output/result.csv");
  ic_impl::WriteExactShares(sctx.get(), vif_shared, output+".shares.json");
  ic_impl::WriteRecipientResult(sctx.get(), vif_shared, output, ctx_->io_param.result_to_rank);
}
void SsVifHandler::SaveVectorToOutput(const spu::Value&) {}
}  // namespace ic_impl::algo::ss_vif
