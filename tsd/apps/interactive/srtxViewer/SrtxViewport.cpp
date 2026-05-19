// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#include "SrtxViewport.h"
// tsd_ui_imgui
#include "imgui.h"
#include "tsd/ui/imgui/Application.h"
// tsd_core
#include "tsd/core/Logging.hpp"
// stb_image
#include "stb_image_write.h"
// std
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace {

// Compute the column-major 4x4 camera-local-to-world transform from a
// position + (yaw, pitch) pair. Convention: yaw rotates around world +Y
// (positive turns the view left), pitch is around the camera's local +X.
// At yaw = pitch = 0 the camera faces -Z, right = +X, up = +Y, matching
// the OpenGL/USD camera convention. The returned matrix is laid out in
// column-major order so it can be passed directly via
// anariSetParameter(..., ANARI_FLOAT32_MAT4, m).
void buildCameraTransform(const tsd::math::float3 &pos,
    const tsd::math::float2 &yawPitch,
    float out[16])
{
  const float yaw = yawPitch.x;
  const float pitch = yawPitch.y;
  const float cy = std::cos(yaw);
  const float sy = std::sin(yaw);
  const float cp = std::cos(pitch);
  const float sp = std::sin(pitch);

  // Forward (the direction the camera looks at) in world coords.
  const tsd::math::float3 forward(-sy * cp, sp, -cy * cp);
  // Right hand vector with no roll: perpendicular to forward, lying in the
  // y = 0 plane in camera-local space, mapped to world.
  const tsd::math::float3 right(cy, 0.f, -sy);
  // Up = right x (-forward), giving (right, up, -forward) as an orthonormal
  // right-handed basis matching the OpenGL/USD camera convention.
  const tsd::math::float3 up = cross(right, forward);

  // Column-major storage: column c starts at out[c * 4]. Columns are
  // (right, up, -forward, position); the last row is (0, 0, 0, 1).
  out[0]  = right.x;   out[1]  = right.y;   out[2]  = right.z;   out[3]  = 0.f;
  out[4]  = up.x;      out[5]  = up.y;      out[6]  = up.z;      out[7]  = 0.f;
  out[8]  = -forward.x;out[9]  = -forward.y;out[10] = -forward.z;out[11] = 0.f;
  out[12] = pos.x;     out[13] = pos.y;     out[14] = pos.z;     out[15] = 1.f;
}

} // namespace

static void srtxStatusFunc(const void * /*userData*/,
    ANARIDevice /*device*/,
    ANARIObject /*source*/,
    ANARIDataType /*sourceType*/,
    ANARIStatusSeverity severity,
    ANARIStatusCode /*code*/,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR)
    tsd::core::logError("[SRTX] %s", message);
  else if (severity == ANARI_SEVERITY_ERROR)
    tsd::core::logError("[SRTX] %s", message);
  else if (severity == ANARI_SEVERITY_WARNING)
    tsd::core::logWarning("[SRTX] %s", message);
  else if (severity == ANARI_SEVERITY_INFO)
    tsd::core::logStatus("[SRTX] %s", message);
  else
    tsd::core::logDebug("[SRTX] %s", message);
}

namespace tsd_srtx {

SrtxViewport::SrtxViewport(tsd::ui::imgui::Application *app,
    tsd::rendering::Manipulator *m,
    const char *name)
    : BaseViewport(app, name)
{
  setManipulator(m);
}

SrtxViewport::~SrtxViewport()
{
  teardownDevice();
  BaseViewport::imagePipeline_teardown();
}

void SrtxViewport::buildUI()
{
  BaseViewport::buildUI();

  if (!imagePipeline_isSetup())
  {
    imagePipeline_setup();
    m_clearPass->setClearColor(tsd::math::float4(0.1f, 0.1f, 0.1f, 1.f));
  }

  if (m_deviceReady)
  {
    renderFrame();
  }

  m_incomingFramePass->setEnabled(m_deviceReady);
  BaseViewport::imagePipeline_render();

  ui_menubar();

  if (m_outputPass)
  {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    // Fit the SRTX-resolution texture into the dock content area while
    // preserving the render aspect ratio (letterbox/pillarbox the rest).
    // Until the first frame arrives, the pipeline is sized to the dock area
    // so a 1:1 fit is correct as a fallback.
    ImVec2 imageSize = avail;
    if (m_renderSize.x > 0 && m_renderSize.y > 0
        && avail.x > 0.f && avail.y > 0.f)
    {
      const float renderAspect =
          float(m_renderSize.x) / float(m_renderSize.y);
      const float availAspect = avail.x / avail.y;
      if (availAspect > renderAspect)
        imageSize.x = avail.y * renderAspect;
      else
        imageSize.y = avail.x / renderAspect;
    }

    const ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - imageSize.x) * 0.5f,
        cursor.y + (avail.y - imageSize.y) * 0.5f));
    ImGui::Image((ImTextureID)m_outputPass->getTexture(),
        imageSize,
        ImVec2(0, 1),
        ImVec2(1, 0));
  }

  if (m_showOverlay)
    ui_overlay();
}

void SrtxViewport::saveSettings(tsd::core::DataNode &root)
{
  BaseViewport::saveSettings(root);
  root["srtx.serverUrl"] = m_serverUrl;
  root["srtx.stageUrl"] = m_stageUrl;
  root["srtx.renderProductPath"] = m_renderProductPath;
  root["srtx.compressionType"] = m_compressionType;
  root["srtx.cameraPath"] = m_cameraPath;
  root["srtx.flySpeed"] = m_flySpeed;
  root["srtx.lookSensitivity"] = m_lookSensitivity;
  root["srtx.resolutionMode"] = static_cast<int>(m_resolutionMode);
  root["srtx.customResolution.x"] = m_customResolution.x;
  root["srtx.customResolution.y"] = m_customResolution.y;
}

void SrtxViewport::loadSettings(tsd::core::DataNode &root)
{
  BaseViewport::loadSettings(root);

  {
    std::string val;
    if (root["srtx.serverUrl"].getValue(ANARI_STRING, &val))
      m_serverUrl = val;
    if (root["srtx.stageUrl"].getValue(ANARI_STRING, &val))
      m_stageUrl = val;
    if (root["srtx.renderProductPath"].getValue(ANARI_STRING, &val))
      m_renderProductPath = val;
    if (root["srtx.compressionType"].getValue(ANARI_STRING, &val))
      m_compressionType = val;
    if (root["srtx.cameraPath"].getValue(ANARI_STRING, &val))
      m_cameraPath = val;
  }

  root["srtx.flySpeed"].getValue(ANARI_FLOAT32, &m_flySpeed);
  root["srtx.lookSensitivity"].getValue(ANARI_FLOAT32, &m_lookSensitivity);
  m_flySpeed = std::max(0.f, m_flySpeed);
  m_lookSensitivity = std::max(0.f, m_lookSensitivity);

  int storedMode = static_cast<int>(m_resolutionMode);
  if (root["srtx.resolutionMode"].getValue(ANARI_INT32, &storedMode))
  {
    if (storedMode < 0
        || storedMode > static_cast<int>(ResolutionMode::MatchViewport))
      storedMode = static_cast<int>(ResolutionMode::UsdDefault);
    m_resolutionMode = static_cast<ResolutionMode>(storedMode);
  }
  root["srtx.customResolution.x"].getValue(ANARI_INT32, &m_customResolution.x);
  root["srtx.customResolution.y"].getValue(ANARI_INT32, &m_customResolution.y);
  m_customResolution.x = std::max(1, m_customResolution.x);
  m_customResolution.y = std::max(1, m_customResolution.y);

  if (!m_serverUrl.empty() && !m_stageUrl.empty())
  {
    m_paramsChanged = true;
    setupDevice();
  }
}

void SrtxViewport::viewport_reshape(tsd::math::int2 newWindowSize)
{
  if (newWindowSize.x <= 0 || newWindowSize.y <= 0)
    return;

  m_viewport.size = newWindowSize;
  m_viewport.renderSize = tsd::math::int2(
      tsd::math::float2(m_viewport.size) * m_viewport.resolutionScale);

  // The pipeline and SDL texture are sized to the SRTX render resolution,
  // not the dock area. Until the first frame arrives, fall back to the dock
  // size so that imagePipeline_render() has a valid texture to upload to;
  // afterwards, dock changes only affect on-screen layout and are absorbed
  // by the aspect-correct ImGui::Image draw in buildUI().
  if (m_renderSize.x <= 0 || m_renderSize.y <= 0)
  {
    BaseViewport::imagePipeline_setDimensions(
        m_viewport.renderSize.x, m_viewport.renderSize.y);
  }
}

void SrtxViewport::imagePipeline_populate(tsd::rendering::RenderPipeline &p)
{
  m_clearPass = p.emplace_back<tsd::rendering::ClearBuffersPass>();
  m_incomingFramePass = p.emplace_back<tsd::rendering::CopyToColorBufferPass>();
  m_outputPass = p.emplace_back<tsd::rendering::CopyToSDLTexturePass>(
      m_app->sdlRenderer());

  m_incomingFramePass->setExternalBuffer(m_colorBuffer);
  m_incomingFramePass->setEnabled(false);
}

void SrtxViewport::camera_resetView(bool /*resetAzEl*/)
{
  tsd::core::logWarning(
      "Camera view reset is not supported in SRTX viewport"
      " (camera is defined in the USD stage).");
}

void SrtxViewport::camera_centerView()
{
  tsd::core::logWarning(
      "Camera center view is not supported in SRTX viewport"
      " (camera is defined in the USD stage).");
}

void SrtxViewport::renderer_resetParameterDefaults()
{
  tsd::core::logWarning(
      "Renderer parameter reset is not supported in SRTX viewport.");
}

void SrtxViewport::setupDevice()
{
  teardownDevice();

  m_statusMessage = "Loading SRTX device...";

  m_library = anariLoadLibrary("srtx", srtxStatusFunc, nullptr);
  if (!m_library)
  {
    m_statusMessage = "Failed to load SRTX library";
    tsd::core::logError("%s", m_statusMessage.c_str());
    return;
  }

  m_device = anariNewDevice(m_library, "default");
  if (!m_device)
  {
    m_statusMessage = "Failed to create SRTX device";
    tsd::core::logError("%s", m_statusMessage.c_str());
    anariUnloadLibrary(m_library);
    m_library = nullptr;
    return;
  }

  anariCommitParameters(m_device, m_device);

  m_renderer = anariNewRenderer(m_device, "default");
  m_frame = anariNewFrame(m_device);

  m_statusMessage = "SRTX device loaded";
  tsd::core::logStatus("%s", m_statusMessage.c_str());
}

void SrtxViewport::teardownDevice()
{
  m_deviceReady = false;

  if (m_frame)
  {
    anariRelease(m_device, m_frame);
    m_frame = nullptr;
  }
  if (m_renderer)
  {
    anariRelease(m_device, m_renderer);
    m_renderer = nullptr;
  }
  if (m_device)
  {
    anariRelease(m_device, m_device);
    m_device = nullptr;
  }
  if (m_library)
  {
    anariUnloadLibrary(m_library);
    m_library = nullptr;
  }

  m_statusMessage.clear();
}

void SrtxViewport::applyParameters()
{
  if (!m_device || !m_renderer || !m_frame)
    return;

  anariSetParameter(m_device,
      m_renderer,
      "srtx::serverUrl",
      ANARI_STRING,
      m_serverUrl.c_str());
  anariCommitParameters(m_device, m_renderer);

  anariSetParameter(m_device,
      m_frame,
      "srtx::stageUrl",
      ANARI_STRING,
      m_stageUrl.c_str());
  anariSetParameter(m_device,
      m_frame,
      "srtx::renderProductPath",
      ANARI_STRING,
      m_renderProductPath.c_str());

  if (!m_compressionType.empty())
  {
    anariSetParameter(m_device,
        m_frame,
        "srtx::compressionType",
        ANARI_STRING,
        m_compressionType.c_str());
  }

  // Camera-path is committed once on connect; the actual transform is only
  // pushed once the user navigates (or hits "Reset camera"), via
  // pushCameraTransformIfNeeded() from the per-frame path.
  if (!m_cameraPath.empty())
  {
    anariSetParameter(m_device,
        m_frame,
        "srtx::cameraPath",
        ANARI_STRING,
        m_cameraPath.c_str());
  }

  // Compute the initial desired resolution from the current ResolutionMode,
  // then push it as the frame's "size" so the device-side resolution write
  // happens as part of the very first commit.
  updateDesiredResolution();
  uint32_t size[2] = {
      (uint32_t)std::max(0, m_desiredResolution.x),
      (uint32_t)std::max(0, m_desiredResolution.y)};
  anariSetParameter(m_device, m_frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(
      m_device, m_frame, "srtx::changenumber", ANARI_INT32, &m_resolutionChangeNumber);

  anariSetParameter(
      m_device, m_frame, "channel.color", ANARI_DATA_TYPE, &ANARI_UFIXED8_RGBA_SRGB);
  anariSetParameter(
      m_device, m_frame, "renderer", ANARI_RENDERER, &m_renderer);

  anariCommitParameters(m_device, m_frame);

  m_lastSentResolution = m_desiredResolution;
  m_paramsChanged = false;
  m_deviceReady = true;
  m_statusMessage = "Connected to " + m_serverUrl;
  tsd::core::logStatus(
      "SRTX parameters applied: server=%s stage=%s renderProduct=%s",
      m_serverUrl.c_str(),
      m_stageUrl.c_str(),
      m_renderProductPath.c_str());
}

void SrtxViewport::updateDesiredResolution()
{
  switch (m_resolutionMode)
  {
  case ResolutionMode::UsdDefault:
    // Tell the device to leave the server's render product alone by sending
    // a zero size; SrtxFrame skips the write in that case.
    m_desiredResolution = tsd::math::int2(0, 0);
    m_pendingMatchSize = tsd::math::int2(0, 0);
    break;
  case ResolutionMode::Custom:
    m_desiredResolution = tsd::math::int2(
        std::max(1, m_customResolution.x), std::max(1, m_customResolution.y));
    m_pendingMatchSize = tsd::math::int2(0, 0);
    break;
  case ResolutionMode::MatchViewport:
  {
    // Debounce so resizing the dock by dragging doesn't fire a write per
    // frame; only commit a new size after the dock has been stable for the
    // debounce interval.
    constexpr double kMatchDebounceMs = 250.0;
    const double nowMs = ImGui::GetTime() * 1000.0;
    if (m_pendingMatchSize != m_viewport.size)
    {
      m_pendingMatchSize = m_viewport.size;
      m_pendingMatchExpiresMs = nowMs + kMatchDebounceMs;
    }
    if (nowMs >= m_pendingMatchExpiresMs && m_pendingMatchSize.x > 0
        && m_pendingMatchSize.y > 0)
    {
      m_desiredResolution = m_pendingMatchSize;
    }
    break;
  }
  }
}

void SrtxViewport::pushResolutionParametersIfNeeded()
{
  if (!m_device || !m_frame)
    return;

  updateDesiredResolution();

  if (m_desiredResolution == m_lastSentResolution)
    return;

  // Bump the change number so the server treats this resize as a new scene
  // state; SrtxFrame forwards it to USDWriteService::WriteSceneValues.
  m_resolutionChangeNumber++;

  uint32_t size[2] = {
      (uint32_t)std::max(0, m_desiredResolution.x),
      (uint32_t)std::max(0, m_desiredResolution.y)};
  anariSetParameter(m_device, m_frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(
      m_device, m_frame, "srtx::changenumber", ANARI_INT32, &m_resolutionChangeNumber);
  anariCommitParameters(m_device, m_frame);

  m_lastSentResolution = m_desiredResolution;
}

void SrtxViewport::handleFlyInput()
{
  // Fly mode is press-and-hold on the right mouse button. While inactive,
  // we deliberately ignore keyboard so WASD/QE never collide with global
  // shortcuts (the gizmo bindings on W/E/R/Q in BaseViewport hover-check
  // before consuming, which is compatible with the SRTX viewport even when
  // the SRTX dock node is the focused window).
  const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);
  const bool hovered = ImGui::IsWindowHovered();

  if (!m_flyActive)
  {
    if (rmb && hovered)
    {
      m_flyActive = true;
      const ImVec2 mp = ImGui::GetIO().MousePos;
      m_flyPrevMouse = tsd::math::float2(mp.x, mp.y);
    }
    return;
  }

  // We were active last frame. Stop on release, regardless of hover, so the
  // pose doesn't drift if the cursor wanders off the dock during a drag.
  if (!rmb)
  {
    m_flyActive = false;
    return;
  }

  ImGuiIO &io = ImGui::GetIO();
  const tsd::math::float2 mouse(io.MousePos.x, io.MousePos.y);
  const tsd::math::float2 delta = mouse - m_flyPrevMouse;
  m_flyPrevMouse = mouse;

  if (m_cameraPath.empty())
    return; // Without a target prim there is nothing to drive; ignore input.

  // ----- DEBUG GUARD: per-frame motion caps ---------------------------------
  // While the SRTX pipeline is rendering at < 1 FPS, ImGui delivers the
  // entire accumulated input delta of the previous render interval as a
  // single tick. Without bounds, a ~1 s mouse swipe can rotate the camera
  // multiple full turns and walk it out of the scene in one commit. Cap
  // both the angular delta and the translation step so we can observe
  // whether the wire-up is correct independent of render performance.
  //
  // Remove or relax these once the pipeline runs at interactive rates.
  constexpr float kMaxYawDeltaPerFrame = 0.26179938f;   // ~15 deg
  constexpr float kMaxPitchDeltaPerFrame = 0.26179938f; // ~15 deg
  constexpr float kMaxTranslationDt = 0.05f;            // == 1/20 s
  // --------------------------------------------------------------------------

  // Mouse-look. Positive horizontal mouse delta turns the view right (yaw
  // decreases under the right-handed convention used by buildCameraTransform).
  // Vertical delta is inverted so pulling the mouse down tilts the view up,
  // matching the typical first-person feel.
  if (delta.x != 0.f || delta.y != 0.f)
  {
    float dYaw = -delta.x * m_lookSensitivity;
    float dPitch = -delta.y * m_lookSensitivity;
    dYaw = std::clamp(dYaw, -kMaxYawDeltaPerFrame, kMaxYawDeltaPerFrame);
    dPitch = std::clamp(dPitch, -kMaxPitchDeltaPerFrame, kMaxPitchDeltaPerFrame);
    m_camYawPitch.x += dYaw;
    m_camYawPitch.y += dPitch;
    constexpr float kPitchLimit = 1.553343f; // ~89 deg in radians
    m_camYawPitch.y = std::clamp(m_camYawPitch.y, -kPitchLimit, kPitchLimit);
    m_cameraDirty = true;
    m_hasUserPose = true;
  }

  // WASD + QE movement. dt comes from ImGui so it tracks framerate even
  // when the SRTX render pipeline runs at a different cadence -- but we cap
  // the effective dt (debug guard above) so the per-frame step stays small
  // at very low framerates.
  float dt = io.DeltaTime;
  if (dt <= 0.f || m_flySpeed <= 0.f)
    return;
  dt = std::min(dt, kMaxTranslationDt);

  const float yaw = m_camYawPitch.x;
  const float pitch = m_camYawPitch.y;
  const float cy = std::cos(yaw);
  const float sy = std::sin(yaw);
  const float cp = std::cos(pitch);
  const float sp = std::sin(pitch);
  const tsd::math::float3 forward(-sy * cp, sp, -cy * cp);
  const tsd::math::float3 right(cy, 0.f, -sy);
  const tsd::math::float3 worldUp(0.f, 1.f, 0.f);

  // Boost: shift accelerates; ctrl decelerates. Both stack with m_flySpeed.
  float scale = m_flySpeed * dt;
  if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
    scale *= 4.f;
  if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
    scale *= 0.25f;

  tsd::math::float3 step(0.f, 0.f, 0.f);
  if (ImGui::IsKeyDown(ImGuiKey_W))
    step += forward;
  if (ImGui::IsKeyDown(ImGuiKey_S))
    step -= forward;
  if (ImGui::IsKeyDown(ImGuiKey_D))
    step += right;
  if (ImGui::IsKeyDown(ImGuiKey_A))
    step -= right;
  if (ImGui::IsKeyDown(ImGuiKey_E))
    step += worldUp;
  if (ImGui::IsKeyDown(ImGuiKey_Q))
    step -= worldUp;

  if (step.x != 0.f || step.y != 0.f || step.z != 0.f)
  {
    m_camPosition += step * scale;
    m_cameraDirty = true;
    m_hasUserPose = true;
  }
}

bool SrtxViewport::seedCameraFromUsd()
{
  // Best-effort one-shot read of the camera's existing worldMatrix from the
  // server. The result is used purely to initialize our local fly-cam pose
  // so the first navigation input is relative to the USD-authored camera
  // rather than to our default starting pose. We deliberately do NOT mark
  // m_cameraDirty or set m_hasUserPose here: regardless of whether seeding
  // succeeds, the USD-defined pose remains in effect on the server until
  // the user actually navigates.
  if (!m_device || !m_frame)
    return false;
  if (m_cameraPath.empty())
    return false;
  if (m_hasUserPose)
    return false; // User already has a meaningful local pose; do not stomp it.

  // The device reads srtx::cameraWorldMatrix relative to its currently
  // committed srtx::cameraPath. Push the viewport's latest m_cameraPath
  // (which may have just been edited via the text field) and commit so the
  // query targets the right prim, not whatever was committed at connect time
  // / by the last transform push.
  anariSetParameter(m_device,
      m_frame,
      "srtx::cameraPath",
      ANARI_STRING,
      m_cameraPath.c_str());
  anariCommitParameters(m_device, m_frame);

  float worldMatrix[16] = {};
  if (!anariGetProperty(m_device,
          m_frame,
          "srtx::cameraWorldMatrix",
          ANARI_FLOAT32_MAT4,
          worldMatrix,
          sizeof(worldMatrix),
          ANARI_WAIT))
  {
    // No matrix available. Caller decides whether to log / retry; we keep
    // this path silent so the renderFrame() retry loop can drive the seed
    // without spamming the status line on every attempt.
    return false;
  }

  // ANARI mat4 is column-major: column c starts at index c * 4. Position is
  // column 3 (translation), camera basis is (right, up, -forward) in columns
  // 0..2. Recover yaw/pitch from -forward (which our buildCameraTransform()
  // writes into column 2). Reject obviously non-orthonormal results (e.g.
  // mirrored or zero-scaled matrices) by checking the forward column's
  // length; we cannot represent those with (yaw, pitch) anyway.
  const tsd::math::float3 position(
      worldMatrix[12], worldMatrix[13], worldMatrix[14]);
  const tsd::math::float3 negForward(
      worldMatrix[8], worldMatrix[9], worldMatrix[10]);
  const float fLen = std::sqrt(negForward.x * negForward.x
      + negForward.y * negForward.y + negForward.z * negForward.z);
  if (fLen < 1e-6f)
    return false;
  const tsd::math::float3 forward(
      -negForward.x / fLen, -negForward.y / fLen, -negForward.z / fLen);

  // Inverse of buildCameraTransform()'s forward = (-sy*cp, sp, -cy*cp):
  //   pitch = asin(forward.y)
  //   yaw   = atan2(-forward.x, -forward.z)
  // Both well-defined because |forward| == 1 and pitch lies in (-pi/2, pi/2).
  const float pitch = std::asin(std::clamp(forward.y, -1.f, 1.f));
  const float yaw = std::atan2(-forward.x, -forward.z);

  m_camPosition = position;
  m_camYawPitch = tsd::math::float2(yaw, pitch);

  m_statusMessage = "Seeded fly-cam from '" + m_cameraPath + "'";
  tsd::core::logStatus(
      "SRTX seeded fly-cam from '%s': pos=(%.3f, %.3f, %.3f) yaw=%.3f pitch=%.3f",
      m_cameraPath.c_str(),
      position.x,
      position.y,
      position.z,
      yaw,
      pitch);
  return true;
}

void SrtxViewport::pushCameraTransformIfNeeded()
{
  if (!m_device || !m_frame)
    return;
  if (m_cameraPath.empty())
    return;
  if (!m_cameraDirty)
    return;

  m_resolutionChangeNumber++;

  float transform[16];
  buildCameraTransform(m_camPosition, m_camYawPitch, transform);
  anariSetParameter(
      m_device, m_frame, "srtx::cameraPath", ANARI_STRING, m_cameraPath.c_str());
  anariSetParameter(
      m_device, m_frame, "srtx::cameraTransform", ANARI_FLOAT32_MAT4, transform);
  anariSetParameter(
      m_device, m_frame, "srtx::changenumber", ANARI_INT32, &m_resolutionChangeNumber);
  anariCommitParameters(m_device, m_frame);

  m_cameraDirty = false;
}

void SrtxViewport::renderFrame()
{
  if (!m_deviceReady || !m_frame)
    return;

  // Fly-through navigation runs before any device commits so the pose
  // generated by this frame's input becomes part of the same commit batch as
  // the pending resolution change (if any). Both pushers share
  // m_resolutionChangeNumber so the server sees a monotonically increasing
  // scene-state version for each batch.
  handleFlyInput();

  // Resolution control: push a new server-side resolution if the active mode
  // calls for one (Custom value changed, MatchViewport debounce expired). This
  // re-commits the frame before kicking off the next render.
  pushResolutionParametersIfNeeded();

  // Camera control: push a new world matrix to the camera prim if the local
  // fly-cam pose has drifted from the last committed one. Sharing the change
  // number with the resolution path means a single increment is enough to
  // tag both writes for the same render pass.
  pushCameraTransformIfNeeded();

  anariRenderFrame(m_device, m_frame);
  anariFrameReady(m_device, m_frame, ANARI_WAIT);

  uint32_t width = 0, height = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  const void *pixels =
      anariMapFrame(m_device, m_frame, "channel.color", &width, &height, &type);

  if (pixels && width > 0 && height > 0)
  {
    size_t bytesPerPixel = 4;
    size_t totalBytes = width * height * bytesPerPixel;

    if (m_colorBuffer.size() != totalBytes)
      m_colorBuffer.resize(totalBytes);

    std::memcpy(m_colorBuffer.data(), pixels, totalBytes);

    m_lastFrameWidth = width;
    m_lastFrameHeight = height;

    // Resize the pipeline and SDL texture only when the SRTX render
    // resolution actually changes (typically once per session). m_viewport.size
    // is intentionally left untouched so it keeps its BaseViewport meaning
    // ("dock content area") and the dock-driven reshape stops fighting with
    // this code over the same field.
    const tsd::math::int2 incoming((int)width, (int)height);
    if (m_renderSize != incoming)
    {
      m_renderSize = incoming;
      imagePipeline_setDimensions(width, height);
    }

    if (m_saveNextFrame)
    {
      // The SRTX device returns frame data in ANARI convention (origin at
      // the lower-left). PNG files are top-left, so flip on write to match
      // the orientation the user sees on screen via the V-flipped
      // ImGui::Image below.
      stbi_flip_vertically_on_write(1);
      std::string filename =
          "srtx_frame_" + std::to_string(m_screenshotIndex++) + ".png";
      int ok = stbi_write_png(filename.c_str(),
          (int)width,
          (int)height,
          4,
          m_colorBuffer.data(),
          (int)(width * bytesPerPixel));
      if (ok)
      {
        m_statusMessage = "Saved frame to '" + filename + "'";
        tsd::core::logStatus("SRTX frame saved to '%s'", filename.c_str());
      }
      else
      {
        m_statusMessage = "Failed to save frame to '" + filename + "'";
        tsd::core::logError(
            "Failed to save SRTX frame to '%s'", filename.c_str());
      }
      m_saveNextFrame = false;
    }
  }

  anariUnmapFrame(m_device, m_frame, "channel.color");

  // Lazy seed-from-USD. The first attempt right after connect() typically
  // returns false because the server has not yet assigned a change-number
  // to the freshly-created stream; ReadSceneValues rejects the query in
  // that window. We retry once per renderFrame() after the first round-
  // tripped frame so the server has at least one change-number on record,
  // and decrement an attempt budget so a persistently failing path (e.g.
  // a bogus prim) does not spam gRPC + the log forever.
  if (m_seedPending && m_seedAttemptsRemaining > 0)
  {
    --m_seedAttemptsRemaining;
    if (seedCameraFromUsd())
    {
      m_seedPending = false;
    }
    else if (m_seedAttemptsRemaining == 0)
    {
      // Out of retries: surface a one-line summary so the user is not left
      // wondering why the manipulator never bound to their authored pose.
      // The detailed gRPC reason has already been logged via the device's
      // ANARI status callback.
      m_statusMessage = "Camera seed failed for '" + m_cameraPath
          + "' after retries (see log for details)";
      tsd::core::logWarning("%s", m_statusMessage.c_str());
      m_seedPending = false;
    }
  }
}

void SrtxViewport::ui_menubar()
{
  if (ImGui::BeginMenuBar())
  {
    if (ImGui::BeginMenu("SRTX"))
    {
      ImGui::Checkbox("Show Info Overlay", &m_showOverlay);

      ImGui::Separator();

      if (m_deviceReady)
      {
        if (ImGui::MenuItem("Disconnect"))
          teardownDevice();
      }

      ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
  }
}

void SrtxViewport::setServerUrl(std::string value)
{
  if (value == m_serverUrl)
    return;
  m_serverUrl = std::move(value);
  m_paramsChanged = true;
}

void SrtxViewport::setStageUrl(std::string value)
{
  if (value == m_stageUrl)
    return;
  m_stageUrl = std::move(value);
  m_paramsChanged = true;
}

void SrtxViewport::setRenderProductPath(std::string value)
{
  if (value == m_renderProductPath)
    return;
  m_renderProductPath = std::move(value);
  m_paramsChanged = true;
}

void SrtxViewport::setCompressionType(std::string value)
{
  if (value == m_compressionType)
    return;
  m_compressionType = std::move(value);
  m_paramsChanged = true;
}

void SrtxViewport::setCameraPath(std::string value)
{
  if (value == m_cameraPath)
    return;
  m_cameraPath = std::move(value);
  // Only re-push the current pose against the new path when the user has
  // already engaged fly-through navigation. Without this guard, simply
  // typing in a camera path would cause the next renderFrame() to push our
  // default starting pose to the USD camera, clobbering whatever pose was
  // authored in the stage. Matches the same m_hasUserPose gate used by
  // connect()/setupDevice reuse.
  if (m_hasUserPose)
    m_cameraDirty = true;
  // Note: we deliberately do NOT call seedCameraFromUsd() here. With the
  // current per-keystroke InputText handling in SrtxControlPanel, every
  // character would trigger a synchronous gRPC ReadSceneValues. Seeding is
  // anchored to connect() instead; users who want to re-seed from a new
  // camera path can reconnect.
}

void SrtxViewport::setFlySpeed(float value)
{
  m_flySpeed = std::max(0.f, value);
}

void SrtxViewport::setLookSensitivity(float value)
{
  m_lookSensitivity = std::max(0.f, value);
}

void SrtxViewport::applyCameraPath()
{
  // Force a re-seed even if the user has already engaged navigation. The
  // intended workflow is: user edits the camera-path text field, then clicks
  // "Apply Camera Path" to rebind the manipulator to the freshly typed prim.
  // seedCameraFromUsd() normally bails when m_hasUserPose is set so we don't
  // stomp an active fly-cam pose -- we clear that flag here, do the read,
  // and let the seed restore it via the same path. If the read fails (e.g.
  // empty path or the new prim has no fabric worldMatrix yet), m_hasUserPose
  // is left unset so navigation will start fresh from the local default
  // rather than continuing to push the old pose to the new prim.
  m_hasUserPose = false;
  m_cameraDirty = false;
  // Re-seed deferred to renderFrame() so the lazy-retry path uses the same
  // change-number progression as the initial connect-time seed.
  m_seedPending = !m_cameraPath.empty();
  m_seedAttemptsRemaining = m_seedPending ? 8 : 0;
}

void SrtxViewport::setResolutionMode(ResolutionMode mode)
{
  if (mode == m_resolutionMode)
    return;
  m_resolutionMode = mode;
  // ResolutionMode changes are picked up by pushResolutionParametersIfNeeded()
  // on the next renderFrame() pass without needing a full reconnect, so the
  // paramsChanged flag intentionally stays as-is here.
}

void SrtxViewport::setCustomResolution(tsd::math::int2 value)
{
  value.x = std::max(1, value.x);
  value.y = std::max(1, value.y);
  m_customResolution = value;
}

void SrtxViewport::connect()
{
  if (!m_device)
    setupDevice();
  applyParameters();
  // Re-push the current fly-cam pose against the freshly created frame.
  // Without this, a disconnect/reconnect would silently snap back to the
  // USD-default camera transform; setupDevice() makes a new ANARI frame and
  // applyParameters() only ships srtx::cameraPath, not the transform.
  // m_hasUserPose gates this so a first-time connect doesn't trample the
  // USD-authored camera with our local default starting pose.
  if (!m_cameraPath.empty() && m_hasUserPose)
    m_cameraDirty = true;
  // Seed local fly-cam state from the USD-authored camera pose so the very
  // first navigation input starts from "wherever the scene's camera was".
  // Deferred: the immediate read right after connect tends to fail because
  // the server has not yet assigned a change-number to the new stream. The
  // renderFrame() loop retries while m_seedPending is true.
  m_seedPending = !m_cameraPath.empty() && !m_hasUserPose;
  m_seedAttemptsRemaining = m_seedPending ? 8 : 0;
}

void SrtxViewport::disconnect()
{
  teardownDevice();
}

void SrtxViewport::ui_overlay()
{
  ImVec2 contentStart = ImGui::GetCursorStartPos();
  ImVec2 contentAvail = ImGui::GetContentRegionAvail();
  float overlayWidth = 220.f;
  ImGui::SetCursorPos(ImVec2(
      contentStart.x + contentAvail.x - overlayWidth - 4.f,
      contentStart.y + 4.f));

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.7f));

  ImGuiChildFlags childFlags = ImGuiChildFlags_Border
      | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY;
  ImGuiWindowFlags childWindowFlags =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

  if (ImGui::BeginChild(
          "##srtxOverlay", ImVec2(0, 0), childFlags, childWindowFlags))
  {
    ImGui::Text("SRTX Remote Render");
    ImGui::Text("viewport: %i x %i", m_viewport.size.x, m_viewport.size.y);
    if (m_renderSize.x > 0 && m_renderSize.y > 0)
      ImGui::Text("render:   %i x %i", m_renderSize.x, m_renderSize.y);
    if (m_deviceReady)
      ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.f), "Connected");
    else
      ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.f), "Disconnected");
  }
  ImGui::EndChild();

  ImGui::PopStyleColor();
}

} // namespace tsd_srtx
