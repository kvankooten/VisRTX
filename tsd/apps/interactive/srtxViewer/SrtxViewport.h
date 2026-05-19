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

  /////////////////////////////////////////////////////////////////////////////
  // Public surface used by SrtxControlPanel to drive the device. The viewport
  // remains the owner of the ANARI library/device/renderer/frame and of all
  // SRTX-related settings; the panel only forwards user edits and reads
  // status. Any setter that changes a value that needs to be re-sent to the
  // device also marks the parameter state dirty so the next connect() will
  // re-commit. Resolution-mode changes that take effect mid-stream are picked
  // up by pushResolutionParametersIfNeeded() in the existing render path.

  // Connection settings (mirrors of the saved DataNode keys).
  const std::string &serverUrl() const { return m_serverUrl; }
  void setServerUrl(std::string value);
  const std::string &stageUrl() const { return m_stageUrl; }
  void setStageUrl(std::string value);
  const std::string &renderProductPath() const { return m_renderProductPath; }
  void setRenderProductPath(std::string value);
  const std::string &compressionType() const { return m_compressionType; }
  void setCompressionType(std::string value);

  // Path of the USD camera that fly-through navigation drives. Leave empty
  // to leave the USD-defined camera pose untouched (fly mode is then a
  // no-op even if the user presses the navigation keys).
  const std::string &cameraPath() const { return m_cameraPath; }
  void setCameraPath(std::string value);
  // Movement speed of the fly-cam in world units per second. Mouse-look
  // sensitivity (radians per pixel) is exposed similarly so the user can
  // tune both from the control panel.
  float flySpeed() const { return m_flySpeed; }
  void setFlySpeed(float value);
  float lookSensitivity() const { return m_lookSensitivity; }
  void setLookSensitivity(float value);

  // Re-seed the local fly-cam pose from the worldMatrix authored on the
  // currently configured camera path. Equivalent to "rebind the manipulator
  // to this prim and start from where it currently sits". Forces the read
  // even when the user has already engaged navigation, so it can be used as
  // a UI verb after the user has typed a new camera path. Does not push
  // anything to the server on its own; the very next render with the user
  // navigating that prim will commit the next pose.
  void applyCameraPath();

  // Resolution mode + Custom-mode value. Reading m_pendingMatchSize from the
  // panel is helpful for the MatchViewport tooltip.
  ResolutionMode resolutionMode() const { return m_resolutionMode; }
  void setResolutionMode(ResolutionMode mode);
  tsd::math::int2 customResolution() const { return m_customResolution; }
  void setCustomResolution(tsd::math::int2 value);
  tsd::math::int2 pendingMatchSize() const { return m_pendingMatchSize; }

  // Device-control verbs. connect() also handles first-time device setup.
  void connect();
  void disconnect();
  bool isConnected() const { return m_deviceReady; }
  bool canConnect() const
  {
    // Enabled when we have the bare-minimum URLs *and* either the device is
    // not connected yet (so Connect performs the initial setup), or the user
    // has changed at least one parameter since the last successful connect
    // (so Connect applies the change). This keeps the button active right
    // after disconnect() without requiring a placebo edit in some text
    // field.
    if (m_serverUrl.empty() || m_stageUrl.empty())
      return false;
    return !m_deviceReady || m_paramsChanged;
  }

  // Frame capture trigger + last-frame info for the panel's "Save Frame"
  // button + label.
  void requestSaveFrame() { m_saveNextFrame = true; }
  bool hasRenderedFrame() const
  {
    return m_lastFrameWidth > 0 && m_lastFrameHeight > 0;
  }
  uint32_t lastFrameWidth() const { return m_lastFrameWidth; }
  uint32_t lastFrameHeight() const { return m_lastFrameHeight; }

  const std::string &statusMessage() const { return m_statusMessage; }
  /////////////////////////////////////////////////////////////////////////////

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

  // Fly-through navigation. handleFlyInput() reads mouse/keyboard while the
  // right mouse button is held and updates m_camPosition/m_camYawPitch.
  // pushCameraTransformIfNeeded() bumps the scene change-number and pushes
  // a fresh srtx::cameraTransform / srtx::cameraPath pair when the local
  // pose has drifted from what was last sent to the device. Both are called
  // once per renderFrame() pass.
  void handleFlyInput();
  void pushCameraTransformIfNeeded();

  // Ask the device for the camera prim's current worldMatrix and use it to
  // seed m_camPosition / m_camYawPitch so the first fly-through input starts
  // from the USD-authored pose instead of from the hard-coded default. Best-
  // effort: returns true on success, false on any failure (no client, empty
  // path, server query error, non-orthonormal matrix, etc.) leaving the
  // existing local state alone in the false case.
  //
  // The first attempt right after connect() typically fails because the
  // server has not yet established a change-number for the new stream; the
  // viewport works around this by setting m_seedPending and retrying once
  // per renderFrame() until it succeeds or the attempt budget is exhausted.
  bool seedCameraFromUsd();

  void ui_menubar();
  void ui_overlay();

  // Configuration
  std::string m_serverUrl;
  std::string m_stageUrl;
  // RenderProduct prim path; the SRTX server resolves the bound camera and
  // render settings from this product.
  std::string m_renderProductPath{"/Render/Product"};
  std::string m_compressionType;
  // Path of the USD camera prim driven by the fly-through navigation. When
  // empty, all camera-related updates are skipped and the USD-defined pose
  // is left untouched.
  std::string m_cameraPath;
  bool m_showOverlay{true};

  // Fly-through navigation state. m_camPosition is the camera's world-space
  // location; m_camYawPitch is rotation about world +Y (yaw) and the camera's
  // local +X (pitch). pitch is clamped to (-pi/2, pi/2). m_cameraDirty tracks
  // whether the local pose has changed since the last successful push; it is
  // also set by resetCamera() so a single push will commit on the next frame.
  tsd::math::float3 m_camPosition{0.f, 0.f, 5.f};
  tsd::math::float2 m_camYawPitch{0.f, 0.f};
  bool m_cameraDirty{false};
  // True once the user has actually engaged fly-through navigation. Until
  // this flips, connect()/reconnect() leaves the USD-defined camera pose
  // alone instead of overwriting it with the local default pose that
  // m_camPosition / m_camYawPitch are initialized to above.
  bool m_hasUserPose{false};
  // Deferred-seed bookkeeping. The first ReadSceneValues right after connect
  // typically fails because the server has not assigned a change-number to
  // the new stream yet; renderFrame() retries while m_seedPending is set,
  // decrementing m_seedAttemptsRemaining each try so we eventually give up
  // instead of spamming gRPC + the log on every frame.
  bool m_seedPending{false};
  int m_seedAttemptsRemaining{0};
  // Last frame's RMB-held state and mouse position; used to detect the press
  // event so we capture the cursor at the right moment, and to compute the
  // delta on subsequent frames.
  bool m_flyActive{false};
  tsd::math::float2 m_flyPrevMouse{0.f, 0.f};
  // Navigation tuning surfaced in the panel.
  float m_flySpeed{1.f};      // world units / second when WASD is held
  float m_lookSensitivity{0.005f}; // radians per pixel

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
