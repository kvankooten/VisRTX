// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#pragma once

// tsd_ui_imgui
#include "tsd/ui/imgui/windows/Window.h"
// tsd_rendering
#include "tsd/rendering/index/RenderIndex.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <string>

namespace tsd_usd {

// Panel window for configuring and controlling the ANARI USD device.
// This is a non-viewport window that manages the USD device lifecycle
// and provides UI for setting output location, triggering sync, etc.
struct UsdDevicePanel : public tsd::ui::imgui::Window
{
  UsdDevicePanel(tsd::ui::imgui::Application *app,
      const char *name = "USD Export");
  ~UsdDevicePanel() override;

  void buildUI() override;

  bool isDeviceSetup() const;
  void syncScene();

  // Last written output location (for potential SRTX integration later)
  const std::string &lastOutputUrl() const;

 private:
  void saveSettings(tsd::core::DataNode &thisWindowRoot) override;
  void loadSettings(tsd::core::DataNode &thisWindowRoot) override;

  void setupDevice();
  void teardownDevice();
  void applyMutableDeviceParams();
  void setTimeOnDevice(float time);

  void ui_connectionSettings();
  void ui_outputSettings();
  void ui_controls();
  void ui_status();

  // Configuration
  std::string m_hostName;
  std::string m_location{"./usd_output"};
  bool m_newSession{true};
  bool m_outputBinary{false};
  bool m_outputMaterials{true};
  bool m_outputPreviewSurface{true};
  bool m_outputMdlShader{true};
  bool m_enableSaving{true};
  bool m_autoSync{false};

  // State
  bool m_deviceReady{false};
  float m_lastSyncTime{0.f};
  std::string m_statusMessage;
  std::string m_lastOutputUrl;

  // ANARI objects
  anari::Device m_device{nullptr};
  anari::Frame m_frame{nullptr};
  tsd::rendering::RenderIndex *m_renderIndex{nullptr};
};

} // namespace tsd_usd
