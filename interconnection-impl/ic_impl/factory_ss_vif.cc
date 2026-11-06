// Copyright 2025 Ant Group Co., Ltd.
// Licensed under the Apache License, Version 2.0

#include "ic_impl/algo/ss_vif/ss_vif_handler.h"
#include "ic_impl/factory.h"

namespace ic_impl {

std::unique_ptr<AlgoV2Handler> SsVifHandlerFactory::CreateAlgoV2Handler(
    std::shared_ptr<IcContext> ic_ctx) {
  auto ctx = algo::ss_vif::CreateSsVifContext(std::move(ic_ctx));
  return std::make_unique<algo::ss_vif::SsVifHandler>(std::move(ctx));
}

}  // namespace ic_impl



