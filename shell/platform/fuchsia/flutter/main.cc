// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#if !defined(FUCHSIA_SDK)
#include <trace-provider/provider.h>
#include <trace/event.h>
#endif

#include <cstdlib>

#include "loop.h"
#include "runner.h"
#include "runtime/dart/utils/tempfs.h"

int main(int argc, char const* argv[]) {
  std::unique_ptr<async::Loop> loop(flutter_runner::MakeObservableLoop(true));

#if !defined(FUCHSIA_SDK)
  fbl::unique_ptr<trace::TraceProvider> provider;
  {
    TRACE_DURATION("flutter", "CreateTraceProvider");
    bool already_started;
    // Use CreateSynchronously to prevent loss of early events.
    trace::TraceProvider::CreateSynchronously(
        loop->dispatcher(), "flutter_runner", &provider, &already_started);
  }
#endif

  // Set up the process-wide /tmp memfs.
  dart_utils::SetupRunnerTemp();

  FML_DLOG(INFO) << "Flutter application services initialized.";

  flutter_runner::Runner runner(loop.get());

  loop->Run();

  FML_DLOG(INFO) << "Flutter application services terminated.";

  return EXIT_SUCCESS;
}
