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

#include "ic_impl/context.h"

#include "gflags/gflags.h"
#include <chrono>

#include "ic_impl/util.h"

#include "interconnection/handshake/entry.pb.h"

// gflags库，用于定义命令行参数, 当程序运行时，gflags
// 库将解析命令行输入，并填充相应的 FLAGS_ 变量。如果用户在命令行上提供了
// --parties 参数，那么 FLAGS_parties
// 将被设置为用户提供的值；如果没有提供，它将保持其默认值。
// 定义了FLAGS_parties、FLAGS_rank、FLAGS_ic_version和FLAGS_algo
// 宏定义，用于定义默认的全局变量
DEFINE_string(parties, "127.0.0.1:9530,127.0.0.1:9531",
              "server list, format: host1:port1[,host2:port2, ...].");
DEFINE_int32(rank, 0, "self rank");
DEFINE_int32(ic_version, 2, "handshake request version suggested");
DEFINE_string(algo, "ECDH_PSI", "algorithm suggested");
DEFINE_string(protocol_families, "ecc",
              "comma-separated list of protocol families");

namespace ic_impl {

namespace {
// constexpr关键字, 用于指定变量或函数在编译时应该是常量表达式
constexpr std::array<int32_t, 2> kSupportedVersions{1, 2};

int32_t SuggestedAlgo() {
  return util::GetFlagValue(
      org::interconnection::v2::AlgoType_descriptor(), "ALGO_TYPE_",
      util::GetParamEnv(
          "algo", FLAGS_algo));  // 获取环境变量algo的值, 如果不存在则使用默认值
}

std::vector<int32_t> SuggestedProtocolFamilies() {
  return util::GetFlagValues(
      org::interconnection::v2::ProtocolFamily_descriptor(), "PROTOCOL_FAMILY_",
      util::GetParamEnv(
          "protocol_families",
          FLAGS_protocol_families));  // 获取环境变量protocol_families的值,
                                      // 如果不存在则使用默认值
}

}  // namespace

// 创建互联互通上下文
std::shared_ptr<IcContext> CreateIcContext() {
  std::shared_ptr<IcContext> ic_ctx = std::make_shared<IcContext>();
  ic_ctx->metrics = std::make_shared<TimingMetrics>();
  // 宏定义
  YACL_ENFORCE(util::IsFlagSupported(kSupportedVersions, FLAGS_ic_version));
  ic_ctx->version = FLAGS_ic_version;
  //
  ic_ctx->algo = SuggestedAlgo();

  ic_ctx->protocol_families = SuggestedProtocolFamilies();

  YACL_ENFORCE(!ic_ctx->protocol_families.empty());
  // 先传入默认的参数，再传入用户指定的参数

  SPDLOG_INFO("**********将ic_ctx传入MakeLink**********");
  auto t0 = std::chrono::steady_clock::now();
  ic_ctx->lctx = util::MakeLink(FLAGS_parties, FLAGS_rank);
  auto t1 = std::chrono::steady_clock::now();
  ic_ctx->metrics->link_connect_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

  return ic_ctx;
}

}  // namespace ic_impl
