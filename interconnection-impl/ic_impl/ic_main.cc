#include "ic_impl/audit.h"
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

#include "gflags/gflags.h"
#include "spdlog/spdlog.h"
#include <chrono>
#include <cstdlib>
#include <string>

#include "ic_impl/context.h"
#include "ic_impl/party.h"

namespace {

std::string ReadEnv(const char* key, const char* fallback = "未设置") {
  const char* value = std::getenv(key);
  return value == nullptr || *value == '\0' ? fallback : value;
}

}  // namespace

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  spdlog::set_level(spdlog::level::debug);
  SPDLOG_INFO("**********欢迎使用BFIA互联互通算法插件**********");

  try {
    ic_impl::Audit("lifecycle", "start");
    const auto bfia_protocol = ReadEnv("BFIA_PROTOCOL", "BFIA");
    const auto algorithm = ReadEnv("BFIA_ALGORITHM");
    const auto self_party = ReadEnv("BFIA_SELF_PARTY");
    const auto peer_party = ReadEnv("BFIA_PEER_PARTY");
    const auto self_endpoint = ReadEnv("BFIA_SELF_ENDPOINT");
    const auto peer_endpoint = ReadEnv("BFIA_PEER_ENDPOINT");
    const auto alice_endpoint = ReadEnv("BFIA_ALICE_ENDPOINT");
    const auto bob_endpoint = ReadEnv("BFIA_BOB_ENDPOINT");
    const auto runtime = ReadEnv("BFIA_RUNTIME");
    const auto platform = ReadEnv("BFIA_PLATFORM");
    const auto protocol_family =
        ReadEnv("runtime.component.parameter.protocol_families");

    SPDLOG_INFO(
        "BFIA协议启动：协议={}，算法={}，本方={}，对端={}，本端={}，对端端点={}，双方真实IPv4端点={} <-> {}，运行时={}，平台={}，协议族={}",
        bfia_protocol, algorithm, self_party, peer_party, self_endpoint,
        peer_endpoint, alice_endpoint, bob_endpoint, runtime, platform,
        protocol_family);

    // 创建上下文并建立真实网络链路。
    SPDLOG_INFO("BFIA协议上下文创建开始：准备监听本端并连接对端端点");
    auto total_t0 = std::chrono::steady_clock::now();
    auto ctx = ic_impl::CreateIcContext();
    if (ctx) {
      SPDLOG_INFO(
          "BFIA协议上下文创建成功：真实网络链路上下文已建立，本端={}，对端={}，链路准备耗时={}ms",
          self_endpoint, peer_endpoint, ctx->metrics->link_connect_ms);
      // 每个party各自启动，对象初始化
      ic_impl::Party party(ctx);
      SPDLOG_INFO("BFIA协议开始握手和算法执行：本方={}，算法={}", self_party,
                  algorithm);
      party.Run();
      ctx->lctx->WaitLinkTaskFinish();
      const auto stats=ctx->lctx->GetStats();
      ic_impl::Audit("transport", "drained", {{"sent_bytes",stats->sent_bytes.load()}, {"received_bytes",stats->recv_bytes.load()}, {"send_actions",stats->sent_actions.load()}, {"receive_actions",stats->recv_actions.load()}});
      ic_impl::Audit("lifecycle", "complete", {{"exit_code",0}});
      SPDLOG_INFO(
          "BFIA协议双方通信完成：本方={}，对端={}，端点={} <-> {}",
          self_party, peer_party, self_endpoint, peer_endpoint);
      auto total_t1 = std::chrono::steady_clock::now();
      if (ctx->metrics) {
        ctx->metrics->total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     total_t1 - total_t0)
                                     .count();
        SPDLOG_INFO(
            "Timing summary: total={}ms, link_connect={}ms, dataset={}ms, handshake={}ms, algo={}ms",
            ctx->metrics->total_ms, ctx->metrics->link_connect_ms,
            ctx->metrics->dataset_ms, ctx->metrics->handshake_ms,
            ctx->metrics->algo_ms);
      }
    }
  } catch (const std::exception& e) {
    ic_impl::Audit("lifecycle", "failed", {{"error",e.what()},{"exit_code",-1}});
    SPDLOG_ERROR("BFIA协议执行失败：{}", e.what());
    return -1;
  }

  return 0;
}
