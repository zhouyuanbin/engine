// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

#include "flutter/fml/native_library.h"
#include "flutter/fml/paths.h"
#include "flutter/fml/size.h"
#include "flutter/fml/string_view.h"
#include "flutter/shell/version/version.h"

// Include once for the default enum definition.
#include "flutter/shell/common/switches.h"

#undef SHELL_COMMON_SWITCHES_H_

struct SwitchDesc {
  flutter::Switch sw;
  const fml::StringView flag;
  const char* help;
};

#undef DEF_SWITCHES_START
#undef DEF_SWITCH
#undef DEF_SWITCHES_END

// clang-format off
#define DEF_SWITCHES_START static const struct SwitchDesc gSwitchDescs[] = {
#define DEF_SWITCH(p_swtch, p_flag, p_help) \
  { flutter::Switch:: p_swtch, p_flag, p_help },
#define DEF_SWITCHES_END };
// clang-format on

#if FLUTTER_RUNTIME_MODE != FLUTTER_RUNTIME_MODE_RELEASE && \
    FLUTTER_RUNTIME_MODE != FLUTTER_RUNTIME_MODE_DYNAMIC_RELEASE

// List of common and safe VM flags to allow to be passed directly to the VM.
// clang-format off
static const std::string gDartFlagsWhitelist[] = {
    "--max_profile_depth",
    "--profile_period",
    "--random_seed",
    "--enable_mirrors",
};
// clang-format on

#endif

// Include again for struct definition.
#include "flutter/shell/common/switches.h"

namespace flutter {

void PrintUsage(const std::string& executable_name) {
  std::cerr << std::endl << "  " << executable_name << std::endl << std::endl;

  std::cerr << "Versions: " << std::endl << std::endl;

  std::cerr << "Flutter Engine Version: " << GetFlutterEngineVersion()
            << std::endl;
  std::cerr << "Skia Version: " << GetSkiaVersion() << std::endl;

  std::cerr << "Dart Version: " << GetDartVersion() << std::endl << std::endl;

  std::cerr << "Available Flags:" << std::endl;

  const uint32_t column_width = 80;

  const uint32_t flags_count = static_cast<uint32_t>(Switch::Sentinel);

  uint32_t max_width = 2;
  for (uint32_t i = 0; i < flags_count; i++) {
    auto desc = gSwitchDescs[i];
    max_width = std::max<uint32_t>(desc.flag.size() + 2, max_width);
  }

  const uint32_t help_width = column_width - max_width - 3;

  std::cerr << std::string(column_width, '-') << std::endl;
  for (uint32_t i = 0; i < flags_count; i++) {
    auto desc = gSwitchDescs[i];

    std::cerr << std::setw(max_width)
              << std::string("--") + desc.flag.ToString() << " : ";

    std::istringstream stream(desc.help);
    int32_t remaining = help_width;

    std::string word;
    while (stream >> word && remaining > 0) {
      remaining -= (word.size() + 1);
      if (remaining <= 0) {
        std::cerr << std::endl
                  << std::string(max_width, ' ') << "   " << word << " ";
        remaining = help_width;
      } else {
        std::cerr << word << " ";
      }
    }

    std::cerr << std::endl;
  }
  std::cerr << std::string(column_width, '-') << std::endl;
}

const fml::StringView FlagForSwitch(Switch swtch) {
  for (uint32_t i = 0; i < static_cast<uint32_t>(Switch::Sentinel); i++) {
    if (gSwitchDescs[i].sw == swtch) {
      return gSwitchDescs[i].flag;
    }
  }
  return fml::StringView();
}

#if FLUTTER_RUNTIME_MODE != FLUTTER_RUNTIME_MODE_RELEASE && \
    FLUTTER_RUNTIME_MODE != FLUTTER_RUNTIME_MODE_DYNAMIC_RELEASE

static bool IsWhitelistedDartVMFlag(const std::string& flag) {
  for (uint32_t i = 0; i < fml::size(gDartFlagsWhitelist); ++i) {
    const std::string& allowed = gDartFlagsWhitelist[i];
    // Check that the prefix of the flag matches one of the whitelisted flags.
    // We don't need to worry about cases like "--safe --sneaky_dangerous" as
    // the VM will discard these as a single unrecognized flag.
    if (std::equal(allowed.begin(), allowed.end(), flag.begin())) {
      return true;
    }
  }
  return false;
}

#endif

template <typename T>
static bool GetSwitchValue(const fml::CommandLine& command_line,
                           Switch sw,
                           T* result) {
  std::string switch_string;

  if (!command_line.GetOptionValue(FlagForSwitch(sw), &switch_string)) {
    return false;
  }

  std::stringstream stream(switch_string);
  T value = 0;
  if (stream >> value) {
    *result = value;
    return true;
  }

  return false;
}

std::unique_ptr<fml::Mapping> GetSymbolMapping(std::string symbol_prefix,
                                               std::string native_lib_path) {
  const uint8_t* mapping;
  intptr_t size;

  auto lookup_symbol = [&mapping, &size, symbol_prefix](
                           const fml::RefPtr<fml::NativeLibrary>& library) {
    mapping = library->ResolveSymbol((symbol_prefix + "_start").c_str());
    size = reinterpret_cast<intptr_t>(
        library->ResolveSymbol((symbol_prefix + "_size").c_str()));
  };

  fml::RefPtr<fml::NativeLibrary> library =
      fml::NativeLibrary::CreateForCurrentProcess();
  lookup_symbol(library);

  if (!(mapping && size)) {
    // Symbol lookup for the current process fails on some devices.  As a
    // fallback, try doing the lookup based on the path to the Flutter library.
    library = fml::NativeLibrary::Create(native_lib_path.c_str());
    lookup_symbol(library);
  }

  FML_CHECK(mapping && size) << "Unable to resolve symbols: " << symbol_prefix;
  return std::make_unique<fml::NonOwnedMapping>(mapping, size);
}

Settings SettingsFromCommandLine(const fml::CommandLine& command_line) {
  Settings settings = {};

  // Enable Observatory
  settings.enable_observatory =
      !command_line.HasOption(FlagForSwitch(Switch::DisableObservatory));

  // Set Observatory Port
  if (command_line.HasOption(FlagForSwitch(Switch::DeviceObservatoryPort))) {
    if (!GetSwitchValue(command_line, Switch::DeviceObservatoryPort,
                        &settings.observatory_port)) {
      FML_LOG(INFO)
          << "Observatory port specified was malformed. Will default to "
          << settings.observatory_port;
    }
  }

  // Disable need for authentication codes for VM service communication, if
  // specified.
  settings.disable_service_auth_codes =
      command_line.HasOption(FlagForSwitch(Switch::DisableServiceAuthCodes));

  // Checked mode overrides.
  settings.disable_dart_asserts =
      command_line.HasOption(FlagForSwitch(Switch::DisableDartAsserts));

  settings.ipv6 = command_line.HasOption(FlagForSwitch(Switch::IPv6));

  settings.start_paused =
      command_line.HasOption(FlagForSwitch(Switch::StartPaused));

  settings.enable_dart_profiling =
      command_line.HasOption(FlagForSwitch(Switch::EnableDartProfiling));

  settings.enable_software_rendering =
      command_line.HasOption(FlagForSwitch(Switch::EnableSoftwareRendering));

  settings.endless_trace_buffer =
      command_line.HasOption(FlagForSwitch(Switch::EndlessTraceBuffer));

  settings.trace_startup =
      command_line.HasOption(FlagForSwitch(Switch::TraceStartup));

  settings.skia_deterministic_rendering_on_cpu =
      command_line.HasOption(FlagForSwitch(Switch::SkiaDeterministicRendering));

  settings.verbose_logging =
      command_line.HasOption(FlagForSwitch(Switch::VerboseLogging));

  command_line.GetOptionValue(FlagForSwitch(Switch::FlutterAssetsDir),
                              &settings.assets_path);

  std::string aot_shared_library_name;
  command_line.GetOptionValue(FlagForSwitch(Switch::AotSharedLibraryName),
                              &aot_shared_library_name);

  std::string snapshot_asset_path;
  command_line.GetOptionValue(FlagForSwitch(Switch::SnapshotAssetPath),
                              &snapshot_asset_path);

  std::string vm_snapshot_data_filename;
  command_line.GetOptionValue(FlagForSwitch(Switch::VmSnapshotData),
                              &vm_snapshot_data_filename);

  std::string vm_snapshot_instr_filename;
  command_line.GetOptionValue(FlagForSwitch(Switch::VmSnapshotInstructions),
                              &vm_snapshot_instr_filename);

  std::string isolate_snapshot_data_filename;
  command_line.GetOptionValue(FlagForSwitch(Switch::IsolateSnapshotData),
                              &isolate_snapshot_data_filename);

  std::string isolate_snapshot_instr_filename;
  command_line.GetOptionValue(
      FlagForSwitch(Switch::IsolateSnapshotInstructions),
      &isolate_snapshot_instr_filename);

  if (aot_shared_library_name.size() > 0) {
    settings.application_library_path = aot_shared_library_name;
  } else if (snapshot_asset_path.size() > 0) {
    settings.vm_snapshot_data_path =
        fml::paths::JoinPaths({snapshot_asset_path, vm_snapshot_data_filename});
    settings.vm_snapshot_instr_path = fml::paths::JoinPaths(
        {snapshot_asset_path, vm_snapshot_instr_filename});
    settings.isolate_snapshot_data_path = fml::paths::JoinPaths(
        {snapshot_asset_path, isolate_snapshot_data_filename});
    settings.isolate_snapshot_instr_path = fml::paths::JoinPaths(
        {snapshot_asset_path, isolate_snapshot_instr_filename});
  }

  command_line.GetOptionValue(FlagForSwitch(Switch::CacheDirPath),
                              &settings.temp_directory_path);

  if (settings.icu_initialization_required) {
    command_line.GetOptionValue(FlagForSwitch(Switch::ICUDataFilePath),
                                &settings.icu_data_path);
    if (command_line.HasOption(FlagForSwitch(Switch::ICUSymbolPrefix))) {
      std::string icu_symbol_prefix, native_lib_path;
      command_line.GetOptionValue(FlagForSwitch(Switch::ICUSymbolPrefix),
                                  &icu_symbol_prefix);
      command_line.GetOptionValue(FlagForSwitch(Switch::ICUNativeLibPath),
                                  &native_lib_path);
      settings.icu_mapper = [icu_symbol_prefix, native_lib_path] {
        return GetSymbolMapping(icu_symbol_prefix, native_lib_path);
      };
    }
  }

  settings.use_test_fonts =
      command_line.HasOption(FlagForSwitch(Switch::UseTestFonts));

#if FLUTTER_RUNTIME_MODE != FLUTTER_RUNTIME_MODE_RELEASE && \
    FLUTTER_RUNTIME_MODE != FLUTTER_RUNTIME_MODE_DYNAMIC_RELEASE
  command_line.GetOptionValue(FlagForSwitch(Switch::LogTag), &settings.log_tag);
  std::string all_dart_flags;
  if (command_line.GetOptionValue(FlagForSwitch(Switch::DartFlags),
                                  &all_dart_flags)) {
    std::stringstream stream(all_dart_flags);
    std::string flag;

    // Assume that individual flags are comma separated.
    while (std::getline(stream, flag, ',')) {
      if (!IsWhitelistedDartVMFlag(flag)) {
        FML_LOG(FATAL) << "Encountered blacklisted Dart VM flag: " << flag;
      }
      settings.dart_flags.push_back(flag);
    }
  }

  settings.trace_skia =
      command_line.HasOption(FlagForSwitch(Switch::TraceSkia));
  settings.trace_systrace =
      command_line.HasOption(FlagForSwitch(Switch::TraceSystrace));
#endif

  settings.dump_skp_on_shader_compilation =
      command_line.HasOption(FlagForSwitch(Switch::DumpSkpOnShaderCompilation));

  return settings;
}

}  // namespace flutter
