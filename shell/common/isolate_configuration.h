// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_COMMON_ISOLATE_CONFIGURATION_H_
#define FLUTTER_SHELL_COMMON_ISOLATE_CONFIGURATION_H_

#include <future>
#include <memory>
#include <string>

#include "flutter/assets/asset_manager.h"
#include "flutter/assets/asset_resolver.h"
#include "flutter/common/settings.h"
#include "flutter/fml/macros.h"
#include "flutter/fml/mapping.h"
#include "flutter/fml/memory/weak_ptr.h"
#include "flutter/runtime/dart_isolate.h"

namespace flutter {

class IsolateConfiguration {
 public:
  static std::unique_ptr<IsolateConfiguration> InferFromSettings(
      const Settings& settings,
      std::shared_ptr<AssetManager> asset_manager,
      fml::RefPtr<fml::TaskRunner> io_worker);

  static std::unique_ptr<IsolateConfiguration> CreateForAppSnapshot();

  static std::unique_ptr<IsolateConfiguration> CreateForKernel(
      std::unique_ptr<const fml::Mapping> kernel);

  static std::unique_ptr<IsolateConfiguration> CreateForKernelList(
      std::vector<std::future<std::unique_ptr<const fml::Mapping>>>
          kernel_pieces);

  static std::unique_ptr<IsolateConfiguration> CreateForKernelList(
      std::vector<std::unique_ptr<const fml::Mapping>> kernel_pieces);

  IsolateConfiguration();

  virtual ~IsolateConfiguration();

  bool PrepareIsolate(DartIsolate& isolate);

 protected:
  virtual bool DoPrepareIsolate(DartIsolate& isolate) = 0;

 private:
  FML_DISALLOW_COPY_AND_ASSIGN(IsolateConfiguration);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_COMMON_ISOLATE_CONFIGURATION_H_
