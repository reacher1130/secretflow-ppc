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

#include "ic_impl/algo/psi/v2/psi_context_v2.h"

#include "absl/strings/str_split.h"
#include "gflags/gflags.h"
#include "psi/legacy/bucket_psi.h"
#include "psi/utils/csv_checker.h"

#include "ic_impl/protocol_family/ecc/ecc.h"
#include "ic_impl/util.h"

#include "interconnection/handshake/protocol_family/ecc.pb.h"

DEFINE_string(in_path, "data.csv", "psi data in file path");
DEFINE_string(field_names, "{\"host.0\":\"id1\", \"guest.0\":\"id2\"}", "field names");
DEFINE_string(out_path, "", "psi out file path");

DEFINE_int32(result_to_rank, -1, "which rank gets the result");
DECLARE_bool(disable_handshake);

namespace ic_impl::algo::psi::v2 {

namespace {

std::string GetPsiInputFileName() {
  return util::GetInputFileName(FLAGS_in_path);
}

std::string GetPsiOutputFileName() {
  return util::GetOutputFileName(FLAGS_out_path);
}

// std::string GetPsiInputFileFieldNames() {
//   return util::GetParamEnv("field_names", FLAGS_field_names);
// }

std::vector<std::string> GetPsiInputFileFieldNames(
    const std::shared_ptr<IcContext>& ic_ctx) {
  std::vector<std::string> field_names;
  SPDLOG_INFO("**********FLAGS_disable_handshake={}**********", FLAGS_disable_handshake);
  if (!FLAGS_disable_handshake) {
    // 握手模式下 field_names 通过握手协商，但 ECDH-PSI 握手不协商 field_names
    // 尝试从环境变量读取作为 fallback
    char* json_str = util::GetParamEnv("field_names");
    if (json_str == nullptr) {
      SPDLOG_INFO("**********field_names not in ENV, return empty**********");
      return field_names;
    }
    SPDLOG_INFO("**********field_names from ENV: {}**********", json_str);
    size_t word_size = ic_ctx->lctx->WorldSize();
    field_names.resize(word_size);
    // The Kuscia BFIA component contract accepts either a party keyed JSON
    // object (for example {"host.0":"id", "guest.0":"id"}) or a
    // single column name (for example "id") when every party uses the same
    // key.  Keep the latter form valid so UI-created ECDH jobs do not fail
    // before the PSI runtime starts.
    auto json_object = nlohmann::json::parse(json_str, nullptr, false);
    if (json_object.is_discarded() || !json_object.is_object()) {
      field_names.assign(word_size, std::string(json_str));
      return field_names;
    }
    for (size_t i = 0; i < word_size; ++i) {
      std::string party_id = ic_ctx->lctx->PartyIdByRank(i);
      SPDLOG_INFO("**********party_id[{}]={}, looking up in JSON**********", i, party_id);
      if (json_object.contains(party_id)) {
        field_names[i] = json_object.at(party_id);
      } else {
        SPDLOG_WARN("**********JSON key {} not found, trying alternative**********", party_id);
        // 尝试直接用 rank 作为 key
        field_names[i] = json_object.value(party_id, std::string(""));
      }
      SPDLOG_INFO("**********field_names[{}]={}**********", i, field_names[i]);
    }
    return field_names;
  }

  char* json_str = util::GetParamEnv("field_names");
  SPDLOG_INFO("**********field_names: {}**********", json_str ? json_str : "(null)");
  YACL_ENFORCE(json_str != nullptr, "field_names is not in ENV");
  size_t word_size = ic_ctx->lctx->WorldSize();
  field_names.resize(word_size);
  auto json_object = nlohmann::json::parse(json_str, nullptr, false);
  if (json_object.is_discarded() || !json_object.is_object()) {
    field_names.assign(word_size, std::string(json_str));
    return field_names;
  }
  for (size_t i = 0; i < word_size; ++i) {
    std::string party_id = ic_ctx->lctx->PartyIdByRank(i);
    SPDLOG_INFO("**********party_id[{}]={}**********", i, party_id);
    field_names[i] = json_object.at(party_id);
    SPDLOG_INFO("**********field_names[{}]={}**********", i, field_names[i]);
  }
  return field_names;
}

int32_t SuggestedResultToRank() {
  return util::GetParamEnv("result_to_rank", FLAGS_result_to_rank);
}

}  // namespace

using org::interconnection::v2::protocol::CURVE_TYPE_CURVE25519;
using org::interconnection::v2::protocol::CURVE_TYPE_SM2;
using org::interconnection::v2::protocol::
    HASH_TO_CURVE_STRATEGY_DIRECT_HASH_AS_POINT_X;
using org::interconnection::v2::protocol::HASH_TO_CURVE_STRATEGY_TRY_AND_REHASH;
using org::interconnection::v2::protocol::HASH_TYPE_SHA_256;
using org::interconnection::v2::protocol::POINT_OCTET_FORMAT_UNCOMPRESSED;
using org::interconnection::v2::protocol::POINT_OCTET_FORMAT_X962_COMPRESSED;

std::shared_ptr<EcdhPsiContext> CreateEcdhPsiContext(
    std::shared_ptr<IcContext> ic_context) {
  auto ctx = std::make_shared<EcdhPsiContext>();
  ctx->curve_type = protocol_family::ecc::SuggestedCurveType();
  ctx->hash_type = protocol_family::ecc::SuggestedHashType();
  ctx->hash_to_curve_strategy =
      protocol_family::ecc::SuggestedHash2curveStrategy();
  ctx->point_octet_format = protocol_family::ecc::SuggestedPointOctetFormat();
  ctx->bit_length_after_truncated =
      protocol_family::ecc::SuggestedBitLengthAfterTruncated();
  ctx->result_to_rank = SuggestedResultToRank();  // TODO: check
  ctx->field_names = GetPsiInputFileFieldNames(ic_context);
  ctx->ic_ctx = std::move(ic_context);

  return ctx;
}

std::unique_ptr<::psi::BucketPsi> CreateBucketPsi(const EcdhPsiContext& ctx) {
  ::psi::BucketPsiConfig config;
  config.mutable_input_params()->set_path(GetPsiInputFileName());
  size_t self_rank = ctx.ic_ctx->lctx->Rank();
  auto field_list = absl::StrSplit(ctx.field_names.at(self_rank), ',');
  // auto field_list = absl::StrSplit(GetPsiInputFileFieldNames(), ',');
  // SPDLOG_INFO("ECDH-PSI ID field name in rank {} is {} ", self_rank,
  //             field_list);

  config.mutable_input_params()->mutable_select_fields()->Add(
      field_list.begin(), field_list.end());
  config.mutable_input_params()->set_precheck(false);
  SPDLOG_INFO("**********ECDH-PSI output file name: {}**********", GetPsiOutputFileName());
  config.mutable_output_params()->set_path(GetPsiOutputFileName());
  config.mutable_output_params()->set_need_sort(false);

  config.set_psi_type(::psi::PsiType::ECDH_PSI_2PC);

  if (ctx.result_to_rank == -1) {
    config.set_broadcast_result(true);
  } else {
    config.set_broadcast_result(false);
    config.set_receiver_rank(ctx.result_to_rank);
  }

  switch (ctx.curve_type) {
    case CURVE_TYPE_CURVE25519: {
      config.set_curve_type(::psi::CurveType::CURVE_25519);
      YACL_ENFORCE(ctx.hash_type == HASH_TYPE_SHA_256,
                   "Currently only support sha256 hash for curve25519");
      YACL_ENFORCE(
          ctx.hash_to_curve_strategy ==
              HASH_TO_CURVE_STRATEGY_DIRECT_HASH_AS_POINT_X,
          "Currently only support DIRECT_HASH_AS_POINT_X for curve25519");
      YACL_ENFORCE(ctx.point_octet_format == POINT_OCTET_FORMAT_UNCOMPRESSED,
                   "Currently only support uncompressed format for curve25519");
      break;
    }
    case CURVE_TYPE_SM2: {
      config.set_curve_type(::psi::CurveType::CURVE_SM2);
      YACL_ENFORCE(ctx.hash_type == HASH_TYPE_SHA_256,
                   "Currently only support sha256 hash for sm2");
      YACL_ENFORCE(
          ctx.hash_to_curve_strategy == HASH_TO_CURVE_STRATEGY_TRY_AND_REHASH,
          "Currently only support TRY_AND_REHASH for sm2");
      YACL_ENFORCE(
          ctx.point_octet_format == POINT_OCTET_FORMAT_X962_COMPRESSED,
          "Currently only support ANSI X9.62 compressed format for sm2");
      break;
    }
    default:
      YACL_THROW("Unspecified curve type: {}", ctx.curve_type);
  }

  return std::make_unique<::psi::BucketPsi>(config, ctx.ic_ctx->lctx, true);
}

std::unique_ptr<::psi::CsvChecker> CheckInput(const EcdhPsiContext& ctx) {
  // 检查输入文件，以及定义好求交列
  size_t self_rank = ctx.ic_ctx->lctx->Rank();
  // size_t world_size = ctx.ic_ctx->lctx->WorldSize();
  // YACL_ENFORCE(self_rank < world_size);
  // YACL_ENFORCE(ctx->field_names.size() == world_size);
  // auto field_list = absl::StrSplit(GetPsiInputFileFieldNames(), ',');

  auto field_list = absl::StrSplit(ctx.field_names.at(self_rank), ',');
  std::vector<std::string> selected_fields(field_list.begin(),
                                           field_list.end());
  return ::psi::CheckInput(ctx.ic_ctx->lctx, GetPsiInputFileName(),
                           selected_fields, false, true);
}

}  // namespace ic_impl::algo::psi::v2
