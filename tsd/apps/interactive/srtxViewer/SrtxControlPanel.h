// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#pragma once

// tsd_ui_imgui
#include "tsd/ui/imgui/windows/Window.h"

namespace tsd_srtx {

struct SrtxViewport;

// Dockable side panel that drives a SrtxViewport's connection settings and
// device lifecycle. Mirrors the role UsdDevicePanel plays for the USD export
// viewer: the viewport owns the ANARI device and the rendered image; this
// panel is purely the UI surface for configuring it. The panel does not
// persist anything itself -- saveSettings/loadSettings live on the viewport so
// there is a single source of truth for SRTX-related state.
struct SrtxControlPanel : public tsd::ui::imgui::Window
{
  SrtxControlPanel(tsd::ui::imgui::Application *app,
      SrtxViewport *viewport,
      const char *name = "SRTX Control");
  ~SrtxControlPanel() override = default;

  void buildUI() override;

 private:
  void ui_connectionSettings();
  void ui_resolution();
  void ui_camera();
  void ui_controls();
  void ui_frameCapture();
  void ui_status();

  SrtxViewport *m_viewport{nullptr};
};

} // namespace tsd_srtx
