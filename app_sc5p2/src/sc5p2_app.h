// Space Channel 5 Part 2: the ReXGlue application object (paths, GPU plugin choice, Switch settings).
// Follows sa2-nx's app/src/sa2_app.h, without SA2's DLC, language and PGO parts.

#pragma once

#include <filesystem>
#include <string_view>
#include <system_error>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>

#include "cpu/sa2_enhancements.h"
#include "me/me_native_system.h"
#include "me/sa2_render_mode.h"
#include "sc5p2_capture.h"
#include "sc5p2_test_input.h"

#if defined(SC5P2_BUILTIN_CONFIG)
#include "sc5p2_builtin_config.h"  // generated from app_sc5p2/sc5p2_switch_defaults.toml (CMake)
#endif

class Sc5p2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Sc5p2App>(new Sc5p2App(ctx, "sc5p2", PPCImageConfig));
  }

  // The Switch build's fixed settings (app_sc5p2/sc5p2_switch_defaults.toml, compiled in): applied before sc5p2.toml,
  // which holds only the player's options (and may still override any of these).
  std::string_view BuiltInConfig() const override {
#if defined(SC5P2_BUILTIN_CONFIG)
    return kSc5p2BuiltInConfig;
#else
    return {};
#endif
  }
  // Without an sc5p2.toml next to the NRO, the player's default one is written there (app_sc5p2/sc5p2_switch.toml).
  std::string_view DefaultConfigFile() const override {
#if defined(SC5P2_BUILTIN_CONFIG)
    return kSc5p2PlayerConfig;
#else
    return {};
#endif
  }

  void OnPostInitLogging() override {
    // The runtime defaults to no GPU plugin: select ReXGlue's Xenos implementation, keeping an explicit
    // command-line or config-file choice.
    if (!rex::cvar::HasNonDefaultValue("gpu_plugin")) {
      rex::cvar::SetFlagByName("gpu_plugin", "xenos");
    }
    // sa2-nx's switchable enhancements (the renderer reads their keys): resolved before any guest thread exists.
    sa2::enh::Resolve();
  }

  // Input: the SDK's default drivers, plus the scripted test pad when sc5p2_test_buttons(_file) is set.
  // With sa2_renderer_native = true the graphics system is sa2-nx's native Vulkan renderer (src/me, the Switch's
  // renderer) and the emulated Xenos GPU plugin is not loaded (ReXApp only loads it when config.graphics is empty).
  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.input_factory = sc5p2::test_input::Create;
    sa2::enh::ApplyGpuBoost();
    if (me::native::Enabled()) {
      config.graphics = me::native::CreateGraphicsSystem();
      REXLOG_INFO("graphics: native Vulkan renderer (PM4 ring)");
    }
  }

  // Test-run video recordings from the presenter's guest output (sc5p2_video_s / sc5p2_video_trigger_file).
  void OnPostSetup() override {
    sa2::enh::StartReport();
    sa2::render::RegisterKeys();
    sc5p2::capture::Start([this]() -> rex::ui::Presenter* {
      const auto* rt = runtime();
      const auto* graphics = rt ? rt->graphics_system() : nullptr;
      return graphics ? graphics->presenter() : nullptr;
    });
  }

  void OnShutdown() override { sc5p2::capture::Stop(); }

  // Without --game_data_root, use a game_root/ folder next to the executable (on the Switch:
  // sdmc:/switch/sc5p2-nx/game_root), and on a build tree the project's assets/game_root.
  // An explicit path always wins.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) {
      return;
    }
    std::error_code ec;
    const auto folder = rex::filesystem::GetExecutableFolder();
    if (folder.empty()) {
      return;
    }
    for (auto candidate = folder; !candidate.empty(); candidate = candidate.parent_path()) {
      for (const auto& sub : {std::filesystem::path("game_root"),
                              std::filesystem::path("assets") / "game_root"}) {
        // The folder itself, or only its asset cache (game_root.nxpack + .nxidx, see sdk pack_device.h).
        auto pack = candidate / sub;
        pack += ".nxpack";
        if (std::filesystem::is_regular_file(candidate / sub / "default.xex", ec) ||
            std::filesystem::is_regular_file(pack, ec)) {
          paths.game_data_root = candidate / sub;
          return;
        }
      }
      if (candidate == candidate.parent_path()) {
        break;
      }
    }
  }
};
