// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Allow access to fml::MessageLoop::GetCurrent() in order to flush platform
// thread tasks.
#define FML_USED_ON_EMBEDDER

#include <functional>

#include "flutter/fml/macros.h"
#include "flutter/fml/message_loop.h"
#include "flutter/fml/synchronization/waitable_event.h"
#include "flutter/lib/ui/semantics/semantics_node.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/embedder/tests/embedder_config_builder.h"
#include "flutter/testing/testing.h"

namespace flutter {
namespace testing {

using Embedder11yTest = testing::EmbedderTest;

TEST_F(Embedder11yTest, A11yTreeIsConsistent) {
  auto& context = GetEmbedderContext();

  fml::AutoResetWaitableEvent latch;

  // Called by the Dart text fixture on the UI thread to signal that the C++
  // unittest should resume.
  context.AddNativeCallback(
      "SignalNativeTest", CREATE_NATIVE_ENTRY(([&latch](Dart_NativeArguments) {
        latch.Signal();
      })));

  // Called by test fixture on UI thread to pass data back to this test.
  NativeEntry callback;
  context.AddNativeCallback(
      "NotifyTestData",
      CREATE_NATIVE_ENTRY(([&callback](Dart_NativeArguments args) {
        ASSERT_NE(callback, nullptr);
        callback(args);
      })));

  EmbedderConfigBuilder builder(context);
  builder.SetDartEntrypoint("a11y_main");

  auto engine = builder.LaunchEngine();
  ASSERT_TRUE(engine.is_valid());

  // Wait for initial NotifySemanticsEnabled(false).
  callback = [&](Dart_NativeArguments args) {
    bool enabled = true;
    Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_FALSE(enabled);
    latch.Signal();
  };
  latch.Wait();

  // Enable semantics. Wait for NotifySemanticsEnabled(true).
  callback = [&](Dart_NativeArguments args) {
    bool enabled = false;
    Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_TRUE(enabled);
    latch.Signal();
  };
  auto result = FlutterEngineUpdateSemanticsEnabled(engine.get(), true);
  ASSERT_EQ(result, FlutterEngineResult::kSuccess);
  latch.Wait();

  // Wait for initial accessibility features (reduce_motion == false)
  callback = [&](Dart_NativeArguments args) {
    bool enabled = true;
    Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_FALSE(enabled);
    latch.Signal();
  };
  latch.Wait();

  // Set accessibility features: (reduce_motion == true)
  callback = [&](Dart_NativeArguments args) {
    bool enabled = false;
    Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_TRUE(enabled);
    latch.Signal();
  };
  result = FlutterEngineUpdateAccessibilityFeatures(
      engine.get(), kFlutterAccessibilityFeatureReduceMotion);
  ASSERT_EQ(result, FlutterEngineResult::kSuccess);
  latch.Wait();

  // Wait for UpdateSemantics callback on platform (current) thread.
  int node_count = 0;
  int node_batch_end_count = 0;
  context.SetSemanticsNodeCallback(
      [&node_count, &node_batch_end_count](const FlutterSemanticsNode* node) {
        if (node->id == kFlutterSemanticsNodeIdBatchEnd) {
          ++node_batch_end_count;
        } else {
          ++node_count;
          ASSERT_EQ(1.0, node->transform.scaleX);
          ASSERT_EQ(2.0, node->transform.skewX);
          ASSERT_EQ(3.0, node->transform.transX);
          ASSERT_EQ(4.0, node->transform.skewY);
          ASSERT_EQ(5.0, node->transform.scaleY);
          ASSERT_EQ(6.0, node->transform.transY);
          ASSERT_EQ(7.0, node->transform.pers0);
          ASSERT_EQ(8.0, node->transform.pers1);
          ASSERT_EQ(9.0, node->transform.pers2);
        }
      });

  int action_count = 0;
  int action_batch_end_count = 0;
  context.SetSemanticsCustomActionCallback(
      [&action_count,
       &action_batch_end_count](const FlutterSemanticsCustomAction* action) {
        if (action->id == kFlutterSemanticsCustomActionIdBatchEnd) {
          ++action_batch_end_count;
        } else {
          ++action_count;
        }
      });

  latch.Wait();
  fml::MessageLoop::GetCurrent().RunExpiredTasksNow();
  ASSERT_EQ(4, node_count);
  ASSERT_EQ(1, node_batch_end_count);
  ASSERT_EQ(1, action_count);
  ASSERT_EQ(1, action_batch_end_count);

  // Dispatch a tap to semantics node 42. Wait for NotifySemanticsAction.
  callback = [&](Dart_NativeArguments args) {
    int64_t node_id = 0;
    Dart_GetNativeIntegerArgument(args, 0, &node_id);
    ASSERT_EQ(42, node_id);

    int64_t action_id;
    Dart_GetNativeIntegerArgument(args, 1, &action_id);
    ASSERT_EQ(static_cast<int32_t>(flutter::SemanticsAction::kTap), action_id);

    Dart_Handle semantic_args = Dart_GetNativeArgument(args, 2);
    int64_t data;
    Dart_Handle dart_int = Dart_ListGetAt(semantic_args, 0);
    Dart_IntegerToInt64(dart_int, &data);
    ASSERT_EQ(2, data);

    dart_int = Dart_ListGetAt(semantic_args, 1);
    Dart_IntegerToInt64(dart_int, &data);
    ASSERT_EQ(1, data);
    latch.Signal();
  };
  std::vector<uint8_t> bytes({2, 1});
  result = FlutterEngineDispatchSemanticsAction(
      engine.get(), 42, kFlutterSemanticsActionTap, &bytes[0], bytes.size());
  latch.Wait();

  // Disable semantics. Wait for NotifySemanticsEnabled(false).
  callback = [&](Dart_NativeArguments args) {
    bool enabled = true;
    Dart_GetNativeBooleanArgument(args, 0, &enabled);
    ASSERT_FALSE(enabled);
    latch.Signal();
  };
  result = FlutterEngineUpdateSemanticsEnabled(engine.get(), false);
  ASSERT_EQ(result, FlutterEngineResult::kSuccess);
  latch.Wait();
}

}  // namespace testing
}  // namespace flutter
