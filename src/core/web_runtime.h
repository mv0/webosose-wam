// Copyright (c) 2021-2022 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_WEB_RUNTIME_H_
#define CORE_WEB_RUNTIME_H_

#include <memory>

class WebRuntime {
 public:
  static std::unique_ptr<WebRuntime> Create();
  virtual int Run(int argc, const char** argv) = 0;
};

#endif  // CORE_WEB_RUNTIME_H_
