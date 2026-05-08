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
#include <cstring>

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

  if (m_showSettings)
    ui_settingsPanel();

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
  root["srtx.cameraPath"] = m_cameraPath;
  root["srtx.compressionType"] = m_compressionType;
  root["srtx.showSettings"] = m_showSettings;
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
    if (root["srtx.cameraPath"].getValue(ANARI_STRING, &val))
      m_cameraPath = val;
    if (root["srtx.compressionType"].getValue(ANARI_STRING, &val))
      m_compressionType = val;
  }

  root["srtx.showSettings"].getValue(ANARI_BOOL, &m_showSettings);

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
      "srtx::cameraPath",
      ANARI_STRING,
      m_cameraPath.c_str());

  if (!m_compressionType.empty())
  {
    anariSetParameter(m_device,
        m_frame,
        "srtx::compressionType",
        ANARI_STRING,
        m_compressionType.c_str());
  }

  uint32_t size[2] = {
      (uint32_t)m_viewport.size.x, (uint32_t)m_viewport.size.y};
  anariSetParameter(m_device, m_frame, "size", ANARI_UINT32_VEC2, size);

  anariSetParameter(
      m_device, m_frame, "channel.color", ANARI_DATA_TYPE, &ANARI_UFIXED8_RGBA_SRGB);
  anariSetParameter(
      m_device, m_frame, "renderer", ANARI_RENDERER, &m_renderer);

  anariCommitParameters(m_device, m_frame);

  m_paramsChanged = false;
  m_deviceReady = true;
  m_statusMessage = "Connected to " + m_serverUrl;
  tsd::core::logStatus("SRTX parameters applied: server=%s stage=%s camera=%s",
      m_serverUrl.c_str(),
      m_stageUrl.c_str(),
      m_cameraPath.c_str());
}

void SrtxViewport::renderFrame()
{
  if (!m_deviceReady || !m_frame)
    return;

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
}

void SrtxViewport::ui_menubar()
{
  if (ImGui::BeginMenuBar())
  {
    if (ImGui::BeginMenu("SRTX"))
    {
      ImGui::Checkbox("Show Settings Panel", &m_showSettings);
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

void SrtxViewport::ui_settingsPanel()
{
  // Wide enough for long URLs; fields use full child width (label above input).
  const float maxPanelWidth = 560.f;
  const float padding = 8.f;
  float availW = ImGui::GetContentRegionAvail().x - padding;
  float panelWidth = std::min(maxPanelWidth, std::max(280.f, availW));
  ImVec2 contentStart = ImGui::GetCursorStartPos();
  float menuBarHeight = ImGui::GetFrameHeight();

  ImGui::SetCursorPos(
      ImVec2(contentStart.x + 4.f, contentStart.y + menuBarHeight + 4.f));

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 0.9f));

  ImGuiChildFlags childFlags =
      ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY;
  ImGuiWindowFlags childWindowFlags = ImGuiWindowFlags_NoScrollbar;

  if (ImGui::BeginChild("##srtxSettings",
          ImVec2(panelWidth, 0),
          childFlags,
          childWindowFlags))
  {
    ImGui::Text("SRTX Connection Settings");
    ImGui::Separator();

    char serverBuf[256] = {};
    std::strncpy(serverBuf, m_serverUrl.c_str(), sizeof(serverBuf) - 1);
    ImGui::TextUnformatted("Server URL");
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputText("##srtxServerUrl", serverBuf, sizeof(serverBuf)))
    {
      m_serverUrl = serverBuf;
      m_paramsChanged = true;
    }

    char stageBuf[512] = {};
    std::strncpy(stageBuf, m_stageUrl.c_str(), sizeof(stageBuf) - 1);
    ImGui::TextUnformatted("Stage URL");
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputText("##srtxStageUrl", stageBuf, sizeof(stageBuf)))
    {
      m_stageUrl = stageBuf;
      m_paramsChanged = true;
    }

    char cameraBuf[256] = {};
    std::strncpy(cameraBuf, m_cameraPath.c_str(), sizeof(cameraBuf) - 1);
    ImGui::TextUnformatted("Camera Path");
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputText("##srtxCameraPath", cameraBuf, sizeof(cameraBuf)))
    {
      m_cameraPath = cameraBuf;
      m_paramsChanged = true;
    }

    ImGui::Separator();

    bool canConnect =
        !m_serverUrl.empty() && !m_stageUrl.empty() && m_paramsChanged;

    ImGui::BeginDisabled(!canConnect);
    if (ImGui::Button("Connect & Render"))
    {
      if (!m_device)
        setupDevice();
      applyParameters();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!m_deviceReady);
    if (ImGui::Button("Disconnect"))
      teardownDevice();
    ImGui::EndDisabled();

    ImGui::Separator();

    ImGui::BeginDisabled(
        !m_deviceReady || m_lastFrameWidth == 0 || m_lastFrameHeight == 0);
    if (ImGui::Button("Save Frame as PNG"))
      m_saveNextFrame = true;
    ImGui::EndDisabled();

    if (m_lastFrameWidth > 0 && m_lastFrameHeight > 0)
    {
      ImGui::SameLine();
      ImGui::TextDisabled(
          "(%u x %u)", m_lastFrameWidth, m_lastFrameHeight);
    }

    if (!m_statusMessage.empty())
    {
      ImGui::Separator();
      ImGui::TextWrapped("%s", m_statusMessage.c_str());
    }
  }
  ImGui::EndChild();

  ImGui::PopStyleColor();
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
