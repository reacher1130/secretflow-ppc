#pragma once
#include "ic_impl/audit.h"
#include <fstream>
#include <iomanip>
#include <cstring>
#include "libspu/core/context.h"
#include "libspu/core/value.h"
#include "libspu/core/encoding.h"
#include "spdlog/spdlog.h"

namespace ic_impl {
// Preserve ring elements exactly. Decoding individual shares to float loses the
// low bits required for modular reconstruction and is never a plaintext result.
inline void WriteExactShares(spu::SPUContext* ctx, const spu::Value& value,
                             const std::string& path) {
  std::ofstream out(path);
  YACL_ENFORCE(out, "cannot open share output {}", path);
  const auto bytes = value.data().elsize();
  YACL_ENFORCE(bytes == 8 || bytes == 16, "expected SEMI2K ring share");
  out << "{\"format\":\"bfia-ring-share-v1\",\"rank\":" << ctx->lctx()->Rank()
      << ",\"ring_bits\":" << bytes * 8
      << ",\"fraction_bits\":" << ctx->config().fxp_fraction_bits()
      << ",\"shape\":[";
  for (size_t i=0; i<value.shape().size(); ++i) {
    if (i) out << ',';
    out << value.shape()[i];
  }
  out << "],\"shares_hex\":[";
  bool first=true;
  auto storage=value.data();
  for (auto it=storage.begin(); it!=storage.end(); ++it) {
    if (!first) out << ',';
    first=false;
    out << '\"';
    const auto* raw = reinterpret_cast<const unsigned char*>(it.getRawPtr());
    // Runtime is little-endian linux/amd64; emit canonical big-endian hex.
    for (int i=static_cast<int>(bytes)-1;i>=0;--i)
      out << std::hex << std::setw(2) << std::setfill('0') << unsigned(raw[i]);
    out << '\"';
  }
  out << "]}\n";
  out.flush();
  YACL_ENFORCE(out.good(), "share output write failed");
  Audit("output","written",{{"semantics","ring_share"},{"rank",ctx->lctx()->Rank()},{"elements",value.numel()},{"ring_bits",bytes*8},{"fxp_bits",ctx->config().fxp_fraction_bits()},{"path",path}});
  SPDLOG_INFO("BFIA_OUTPUT semantics=ring_share rank={} elements={} ring_bits={} fxp_bits={} path={} plaintext=false",
              ctx->lctx()->Rank(), value.numel(), bytes*8, ctx->config().fxp_fraction_bits(), path);
}
// Reconstruct only to the recipient selected by the BFIA IO handshake.
// All parties contribute; non-recipients never decode any other party's share.
inline void WriteRecipientResult(spu::SPUContext* ctx, const spu::Value& value,
                                 const std::string& path, int recipient, bool boolean_output=false) {
  const auto rank=ctx->lctx()->Rank(); const auto world=ctx->lctx()->WorldSize();
  YACL_ENFORCE(recipient>=-1 && recipient<static_cast<int>(world), "invalid result recipient");
  auto local=value.data().clone(); const auto size=local.numel()*local.elsize();
  YACL_ENFORCE(local.elsize()==8 || local.elsize()==16, "unsupported share ring");
  for(size_t peer=0;peer<world;++peer) {
    if(peer!=rank && (recipient==-1 || peer==static_cast<size_t>(recipient)))
      ctx->lctx()->SendAsync(peer,yacl::ByteContainerView(local.data(),size),"BFIA_RESULT_RECONSTRUCTION");
  }
  if(recipient!=-1 && rank!=static_cast<size_t>(recipient)) {
    Audit("output","withheld",{{"rank",rank},{"recipient",recipient},{"semantics","no_plaintext"}});
    SPDLOG_INFO("BFIA_OUTPUT semantics=no_plaintext rank={} recipient={}",rank,recipient);
    return;
  }
  auto sum=local.clone();
  for(size_t peer=0;peer<world;++peer) {
    if(peer==rank)continue;
    auto other=ctx->lctx()->Recv(peer,"BFIA_RESULT_RECONSTRUCTION");
    YACL_ENFORCE(other.size()==size,"output share shape mismatch");
    auto* dest=reinterpret_cast<unsigned char*>(sum.data());
    const auto* src=reinterpret_cast<const unsigned char*>(other.data());
    for(int64_t k=0;k<sum.numel();++k) {
      // Unsigned arithmetic deliberately wraps modulo 2^k.
      if(sum.elsize()==8) {
        uint64_t a,b;std::memcpy(&a,dest+8*k,8);std::memcpy(&b,src+8*k,8);a+=b;std::memcpy(dest+8*k,&a,8);
      } else {
        unsigned __int128 a,b;std::memcpy(&a,dest+16*k,16);std::memcpy(&b,src+16*k,16);a+=b;std::memcpy(dest+16*k,&a,16);
      }
    }
  }
  auto pt=spu::getDecodeType(value.dtype());
  spu::NdArrayRef decoded(spu::makePtType(pt),value.shape());
  spu::PtBufferView view(static_cast<void*>(decoded.data()),pt,decoded.shape(),decoded.strides());
  spu::decodeFromRing(sum,value.dtype(),ctx->config().fxp_fraction_bits(),&view);
  std::ofstream out(path);YACL_ENFORCE(out,"cannot open result {}",path);
  out << std::setprecision(9);int64_t idx=0;const auto cols=value.shape().size()>1?value.shape()[1]:1;
  for(auto it=decoded.begin();it!=decoded.end();++it) {
    const float value=*reinterpret_cast<const float*>(it.getRawPtr());
    if (boolean_output) out << (value>0.5f ? "true" : "false");
    else out << value;
    out << (++idx%cols==0?'\n':',');
  }
  out.flush();YACL_ENFORCE(out.good(),"result write failed");
  Audit("output","written",{{"rank",rank},{"recipient",recipient},{"semantics",boolean_output?"comparison_boolean":"plaintext"},{"elements",value.numel()},{"path",path}});
  SPDLOG_INFO("BFIA_OUTPUT semantics=plaintext rank={} recipient={} elements={} path={}",rank,recipient,value.numel(),path);
}
}  // namespace ic_impl
