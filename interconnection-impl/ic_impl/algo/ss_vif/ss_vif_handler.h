#pragma once
#include "ic_impl/handler.h"
#include "ss_vif_context.h"
#include "interconnection/handshake/entry.pb.h"
#include "interconnection/handshake/algos/ss_vif.pb.h"
#include "interconnection/handshake/protocol_family/ss.pb.h"
#include "libspu/core/context.h"
#include "libspu/core/value.h"

namespace ic_impl::algo::ss_vif {

using ic_impl::HandshakeRequestV2;
using ic_impl::HandshakeResponseV2;

class SsVifHandler : public AlgoV2Handler {
 public:
  explicit SsVifHandler(std::shared_ptr<SsVifContext> ctx)
      : AlgoV2Handler(ctx->ic_ctx), ctx_(std::move(ctx)) {}
  ~SsVifHandler() override = default;

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
  void SaveVectorToOutput(const spu::Value& v);

  std::shared_ptr<SsVifContext> ctx_;
  std::vector<std::vector<float>> X_local_;
  std::vector<std::string> headers_;
};

}  // namespace ic_impl::algo::ss_vif
