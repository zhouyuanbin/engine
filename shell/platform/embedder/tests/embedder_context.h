// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_EMBEDDER_TESTS_EMBEDDER_CONTEXT_H_
#define FLUTTER_SHELL_PLATFORM_EMBEDDER_TESTS_EMBEDDER_CONTEXT_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "flutter/fml/closure.h"
#include "flutter/fml/macros.h"
#include "flutter/fml/mapping.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/embedder/tests/embedder_test_gl_surface.h"
#include "flutter/testing/test_dart_native_resolver.h"

namespace flutter {
namespace testing {

using SemanticsNodeCallback = std::function<void(const FlutterSemanticsNode*)>;
using SemanticsActionCallback =
    std::function<void(const FlutterSemanticsCustomAction*)>;

class EmbedderContext {
 public:
  EmbedderContext(std::string assets_path = "");

  ~EmbedderContext();

  const std::string& GetAssetsPath() const;

  const fml::Mapping* GetVMSnapshotData() const;

  const fml::Mapping* GetVMSnapshotInstructions() const;

  const fml::Mapping* GetIsolateSnapshotData() const;

  const fml::Mapping* GetIsolateSnapshotInstructions() const;

  void AddIsolateCreateCallback(fml::closure closure);

  void AddNativeCallback(const char* name, Dart_NativeFunction function);

  void SetSemanticsNodeCallback(SemanticsNodeCallback update_semantics_node);

  void SetSemanticsCustomActionCallback(
      SemanticsActionCallback semantics_custom_action);

  void SetPlatformMessageCallback(
      std::function<void(const FlutterPlatformMessage*)> callback);

 private:
  // This allows the builder to access the hooks.
  friend class EmbedderConfigBuilder;

  std::string assets_path_;
  std::unique_ptr<fml::Mapping> vm_snapshot_data_;
  std::unique_ptr<fml::Mapping> vm_snapshot_instructions_;
  std::unique_ptr<fml::Mapping> isolate_snapshot_data_;
  std::unique_ptr<fml::Mapping> isolate_snapshot_instructions_;
  std::vector<fml::closure> isolate_create_callbacks_;
  std::shared_ptr<TestDartNativeResolver> native_resolver_;
  SemanticsNodeCallback update_semantics_node_callback_;
  SemanticsActionCallback update_semantics_custom_action_callback_;
  std::unique_ptr<EmbedderTestGLSurface> gl_surface_;  // lazy
  std::function<void(const FlutterPlatformMessage*)> platform_message_callback_;

  static VoidCallback GetIsolateCreateCallbackHook();

  static FlutterUpdateSemanticsNodeCallback
  GetUpdateSemanticsNodeCallbackHook();

  static FlutterUpdateSemanticsCustomActionCallback
  GetUpdateSemanticsCustomActionCallbackHook();

  void FireIsolateCreateCallbacks();

  void SetNativeResolver();

  void SetupOpenGLSurface();

  bool GLMakeCurrent();

  bool GLClearCurrent();

  bool GLPresent();

  uint32_t GLGetFramebuffer();

  bool GLMakeResourceCurrent();

  void* GLGetProcAddress(const char* name);

  void PlatformMessageCallback(const FlutterPlatformMessage* message);

  FML_DISALLOW_COPY_AND_ASSIGN(EmbedderContext);
};

}  // namespace testing
}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_EMBEDDER_TESTS_EMBEDDER_CONTEXT_H_
