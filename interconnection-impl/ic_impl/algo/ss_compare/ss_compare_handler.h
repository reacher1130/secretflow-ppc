#pragma once
#include "ic_impl/handler.h"
#include "ss_compare_context.h"
#include "interconnection/handshake/entry.pb.h"
#include "interconnection/handshake/algos/ss_compare.pb.h"
#include "interconnection/handshake/protocol_family/ss.pb.h"
#include "libspu/core/context.h"
#include "libspu/core/value.h"

namespace ic_impl::algo::ss_compare {

using ic_impl::HandshakeRequestV2;
using ic_impl::HandshakeResponseV2;

class SSCompareHandler : public AlgoV2Handler {
 public:
  explicit SSCompareHandler(std::shared_ptr<SSCompareContext> ctx)
      : AlgoV2Handler(ctx->ic_ctx), ctx_(std::move(ctx)) {}
  ~SSCompareHandler() override = default;

 private:
  bool ProcessHandshakeResponse(const HandshakeResponseV2&) override;
  HandshakeRequestV2 BuildHandshakeRequest() override;
  HandshakeResponseV2 BuildHandshakeResponse() override;
  status::ErrorStatus NegotiateHandshakeParams(
      const std::vector<HandshakeRequestV2>&) override;
  bool PrepareDataset() override;
  void RunAlgo() override;

  // internal helpers
  std::unique_ptr<spu::SPUContext> MakeSpuContext();
  spu::Value EncodeToShare(const std::vector<std::vector<float>>& X);
  std::vector<std::vector<float>> LoadLocalCsv();
  std::vector<std::string> LoadHeader();
  void SaveResultToOutput(spu::SPUContext* sctx, const spu::Value& result);

  std::shared_ptr<SSCompareContext> ctx_;
  std::vector<std::vector<float>> X_local_;
  std::vector<std::string> headers_;
  spu::SPUContext* sctx_ = nullptr;  // 保存 SPUContext 指针，用于 reveal 操作
};

}  // namespace ic_impl::algo::ss_compare

