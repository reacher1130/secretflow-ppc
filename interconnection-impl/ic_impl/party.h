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

#pragma once

#include <memory>
#include <variant>

#include "ic_impl/handler.h"

namespace ic_impl {

struct IcContext;
class AlgoHandlerFactory;

class Party {
 public:
  // q:explicit是什么意思
  // a:explicit是C++11引入的一个关键字，用于禁止隐式类型转换。当类构造函数或赋值运算符被声明为explicit时，编译器将不允许隐式调用该构造函数或赋值运算符。这可以防止一些意外的类型转换，提高代码的安全性和可读性。
  explicit Party(std::shared_ptr<IcContext> ctx);

  ~Party();

  void Run();

 private:
  std::unique_ptr<AlgoHandlerFactory> CreateHandlerFactory();

  std::shared_ptr<IcContext> ctx_;
};

}  // namespace ic_impl
