// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#pragma once

// tsd_ui_imgui
#include "tsd/ui/imgui/windows/BaseViewport.h"
// tsd_rendering
#include "tsd/rendering/pipeline/RenderPipeline.h"
#include "tsd/rendering/view/Manipulator.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <string>

namespace tsd_srtx {

struct SrtxViewport : public tsd::ui::imgui::BaseViewport
{
  // How the SRTX server's render resolution is selected. UsdDefault leaves
  // the resolution to the USD stage, Custom uses a fixed user-entered value,
  // and MatchViewport snaps the server's resolution to the dock content area
  // (with a short debounce so we don't spam writes during a drag).
  enum class ResolutionMode
  {
    UsdDefault = 0,
    Custom = 1,
    MatchViewport = 2,
  };

  SrtxViewport(tsd::ui::imgui::Application *app,
      tsd::rendering::Manipulator *m,
      const char *name = "SRTX Viewport");
  ~SrtxViewport() override;

  void buildUI() override;

 private:
  void saveSettings(tsd::core::DataNode &thisWindowRoot) override;
  void loadSettings(tsd::core::DataNode &thisWindowRoot) override;

  // Override BaseViewport::viewport_reshape so dock-area changes update the
  // viewport's logical size (used for layout/picking) without resizing the
  // image pipeline. The pipeline is sized to the SRTX-server-provided render
  // resolution by renderFrame() instead, so the two concepts stop fighting
  // over m_viewport.size as a shared cache.
  void viewport_reshape(tsd::math::int2 newWindowSize) override;

  void imagePipeline_populate(tsd::rendering::RenderPipeline &p) override;

  void camera_resetView(bool resetAzEl = true) override;
  void camera_centerView() override;
  void renderer_resetParameterDefaults() override;

  void setupDevice();
  void teardownDevice();
  void applyParameters();
  void renderFrame();

  // Recompute m_desiredResolution from the current ResolutionMode and
  // (for MatchViewport) update the debounce state.
  void updateDesiredResolution();

  // If the desired resolution differs from what we last sent to the device,
  // bump the resolution change number and push new size/path/changenumber
  // parameters to the ANARI frame. Called once per renderFrame() pass.
  void pushResolutionParametersIfNeeded();

  void ui_menubar();
  void ui_settingsPanel();
  void ui_overlay();

  // Configuration
  std::string m_serverUrl;
  std::string m_stageUrl;
  // RenderProduct prim path; the SRTX server resolves the bound camera and
  // render settings from this product.
  std::string m_renderProductPath{"/Render/Product"};
  std::string m_compressionType;
  bool m_showOverlay{true};
  bool m_showSettings{true};

  // Resolution control: which mode is active, the user's manual choice for
  // Custom mode, the size we last actually sent to the device (debounced
  // against rapid dock changes), and the change-number counter we increment
  // on every push so the server treats each resize as a new scene state.
  ResolutionMode m_resolutionMode{ResolutionMode::UsdDefault};
  tsd::math::int2 m_customResolution{1920, 1080};
  tsd::math::int2 m_desiredResolution{0, 0};
  tsd::math::int2 m_lastSentResolution{0, 0};
  tsd::math::int2 m_pendingMatchSize{0, 0};
  double m_pendingMatchExpiresMs{0.0};
  int m_resolutionChangeNumber{0};

  // Connection state
  bool m_deviceReady{false};
  bool m_paramsChanged{true};
  std::string m_statusMessage;

  // ANARI objects
  anari::Library m_library{nullptr};
  anari::Device m_device{nullptr};
  anari::Renderer m_renderer{nullptr};
  anari::Frame m_frame{nullptr};

  // Display pipeline
  std::vector<uint8_t> m_colorBuffer;
  tsd::rendering::ClearBuffersPass *m_clearPass{nullptr};
  tsd::rendering::CopyToColorBufferPass *m_incomingFramePass{nullptr};
  tsd::rendering::CopyToSDLTexturePass *m_outputPass{nullptr};

  // Resolution at which the SRTX server is rendering (USD-defined). Distinct
  // from m_viewport.size, which tracks the dock content area. The pipeline
  // and SDL texture are sized to this; ImGui::Image then fits the texture
  // into the dock area while preserving aspect ratio.
  tsd::math::int2 m_renderSize{0, 0};

  // Frame capture to PNG
  bool m_saveNextFrame{false};
  int m_screenshotIndex{0};
  uint32_t m_lastFrameWidth{0};
  uint32_t m_lastFrameHeight{0};
};

} // namespace tsd_srtx
