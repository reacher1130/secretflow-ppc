#pragma once
#include "ic_impl/handler.h"
#include "ss_xgb_context.h"
#include "interconnection/handshake/entry.pb.h"
#include "interconnection/handshake/algos/ss_xgb.pb.h"
#include "interconnection/handshake/protocol_family/ss.pb.h"
#include <set>
#include <vector>
#include <map>
#include "libspu/core/context.h"
#include "libspu/core/value.h"

namespace ic_impl::algo::ss_xgb {

using ic_impl::HandshakeRequestV2;
using ic_impl::HandshakeResponseV2;

// 树节点结构体
struct TreeNode {
    bool is_leaf = false;
    int feature_idx = -1;
    float split_value = 0.0f;
    float weight = 0.0f;
    int left_child = -1;
    int right_child = -1;
    std::vector<int> sample_indices;
};

class SsxgbHandler : public AlgoV2Handler {
public:
    explicit SsxgbHandler(std::shared_ptr<SsxgbContext> ctx);
    ~SsxgbHandler() override;
private:
    // AlgoV2Handler 要求的虚函数
    bool ProcessHandshakeResponse(const HandshakeResponseV2&) override;
    HandshakeRequestV2 BuildHandshakeRequest() override;
    HandshakeResponseV2 BuildHandshakeResponse() override;
    status::ErrorStatus NegotiateHandshakeParams(
        const std::vector<HandshakeRequestV2>&) override;
    bool PrepareDataset() override;
    void RunAlgo() override;

    // 分步握手协商
    status::ErrorStatus NegotiateAlgoParams(const std::vector<HandshakeRequestV2>& requests);
    status::ErrorStatus NegotiateProtocolParams(const std::vector<HandshakeRequestV2>& requests);
    status::ErrorStatus NegotiateIoParams(const std::vector<HandshakeRequestV2>& requests);
    status::ErrorStatus NegotiateTtpParams(const std::vector<HandshakeRequestV2>& requests);

    std::shared_ptr<SsxgbContext> ctx_;
    // SSXGBTrainer成员
    int tree_num_;
    int max_depth_;
    float lambda_;
    float gamma_;
    int min_samples_split_ = 2;
    float min_gain_ = 1e-6;
    std::vector<TreeNode> model_;
    spu::SPUContext* sctx_ = nullptr;
    // 创建 SPU 上下文
    std::unique_ptr<spu::SPUContext> MakeSpuContext();
};

} // namespace ic_impl::algo::ss_xgb