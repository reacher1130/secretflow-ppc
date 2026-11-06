// Copyright 2025 Ant Group Co., Ltd.
// Licensed under the Apache License, Version 2.0

#include "ic_impl/algo/ss_compare/ss_compare_handler.h"
#include "ic_impl/factory.h"

namespace ic_impl {

std::unique_ptr<AlgoV2Handler> SsCompareHandlerFactory::CreateAlgoV2Handler(
    std::shared_ptr<IcContext> ic_ctx) {
  auto ctx = algo::ss_compare::CreateSSCompareContext(std::move(ic_ctx));
  return std::make_unique<algo::ss_compare::SSCompareHandler>(std::move(ctx));
}

}  // namespace ic_impl

