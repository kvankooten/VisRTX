// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#include "UsdDevicePanel.h"
// local
#include "UsdDeviceSceneSync.hpp"
// tsd_ui_imgui
#include "imgui.h"
#include "tsd/ui/imgui/Application.h"
#include "tsd/ui/imgui/windows/BaseViewport.h"
// tsd_core
#include "tsd/core/Logging.hpp"
#include "tsd/core/TSDMath.hpp"
// std
#include <algorithm>
#include <cstring>

using namespace tsd::usd_export;

namespace tsd_usd {

UsdDevicePanel::UsdDevicePanel(tsd::ui::imgui::Application *app,
    UsdExportFrameSettings *exportFrames,
    const char *name)
    : Window(app, name)
    , m_exportFrames(
          exportFrames != nullptr ? exportFrames : &m_exportFrameStorage)
{}

UsdDevicePanel::~UsdDevicePanel()
{
  teardownDevice();
}

void UsdDevicePanel::buildUI()
{
  ui_connectionSettings();
  ImGui::Separator();
  ui_outputSettings();
  ImGui::Separator();
  ui_controls();
  ImGui::Separator();
  ui_status();
}

bool UsdDevicePanel::isDeviceSetup() const
{
  return m_deviceReady;
}

void UsdDevicePanel::syncScene()
{
  if (!m_deviceReady)
  {
    tsd::core::logWarning("USD device not setup -- cannot sync scene");
    return;
  }

  tsd::core::logStatus("synchronizing scene to USD device...");

  auto &scene = appCore()->tsd.scene;
  float currentTime = scene.getAnimationTime();
  setTimeOnDevice(currentTime);

  synchronizeUsdDeviceScene(scene,
      m_device,
      *m_renderIndex,
      m_usdRendererIndex,
      m_exportFrames->resolvedSize());

  tsd::core::logStatus("...USD sync complete");
}

const std::string &UsdDevicePanel::lastOutputUrl() const
{
  return m_lastOutputUrl;
}

void UsdDevicePanel::saveSettings(tsd::core::DataNode &root)
{
  root["usd.hostName"] = m_hostName;
  root["usd.location"] = m_location;
  root["usd.newSession"] = m_newSession;
  root["usd.outputBinary"] = m_outputBinary;
  root["usd.outputMaterials"] = m_outputMaterials;
  root["usd.outputPreviewSurface"] = m_outputPreviewSurface;
  root["usd.outputMdlShader"] = m_outputMdlShader;
  root["usd.enableSaving"] = m_enableSaving;
  root["usd.autoSync"] = m_autoSync;
  root["usd.exportWidth"] = static_cast<int>(m_exportFrames->width);
  root["usd.exportHeight"] = static_cast<int>(m_exportFrames->height);
  root["usd.matchViewport"] = m_exportFrames->matchViewport;
}

void UsdDevicePanel::loadSettings(tsd::core::DataNode &root)
{
  {
    std::string val;
    if (root["usd.hostName"].getValue(ANARI_STRING, &val))
      m_hostName = val;
    if (root["usd.location"].getValue(ANARI_STRING, &val))
      m_location = val;
  }

  root["usd.newSession"].getValue(ANARI_BOOL, &m_newSession);
  root["usd.outputBinary"].getValue(ANARI_BOOL, &m_outputBinary);
  root["usd.outputMaterials"].getValue(ANARI_BOOL, &m_outputMaterials);
  root["usd.outputPreviewSurface"].getValue(ANARI_BOOL, &m_outputPreviewSurface);
  root["usd.outputMdlShader"].getValue(ANARI_BOOL, &m_outputMdlShader);
  root["usd.enableSaving"].getValue(ANARI_BOOL, &m_enableSaving);
  root["usd.autoSync"].getValue(ANARI_BOOL, &m_autoSync);

  int iw = static_cast<int>(m_exportFrames->width);
  int ih = static_cast<int>(m_exportFrames->height);
  if (root["usd.exportWidth"].getValue(ANARI_INT32, &iw))
    m_exportFrames->width = static_cast<uint32_t>(std::max(1, iw));
  if (root["usd.exportHeight"].getValue(ANARI_INT32, &ih))
    m_exportFrames->height = static_cast<uint32_t>(std::max(1, ih));
  root["usd.matchViewport"].getValue(ANARI_BOOL, &m_exportFrames->matchViewport);
}

void UsdDevicePanel::setupDevice()
{
  teardownDevice();

  m_statusMessage = "Loading USD device...";

  auto &adm = appCore()->anari;

  std::vector<tsd::app::DeviceInitParam> initParams;
  if (!m_hostName.empty())
  {
    initParams.push_back(
        {"usd::serialize.hostName", tsd::core::Any(m_hostName.c_str())});
  }
  initParams.push_back(
      {"usd::serialize.location", tsd::core::Any(m_location.c_str())});
  initParams.push_back(
      {"usd::serialize.newSession", tsd::core::Any(m_newSession)});
  initParams.push_back(
      {"usd::serialize.outputBinary", tsd::core::Any(m_outputBinary)});
  initParams.push_back(
      {"usd::output.material", tsd::core::Any(m_outputMaterials)});
  initParams.push_back(
      {"usd::output.previewSurfaceShader",
          tsd::core::Any(m_outputPreviewSurface)});
  initParams.push_back(
      {"usd::output.mdlShader", tsd::core::Any(m_outputMdlShader)});
  initParams.push_back(
      {"usd::enableSaving", tsd::core::Any(m_enableSaving)});

  m_device = adm.loadDevice("usd", initParams);
  if (!m_device)
  {
    m_statusMessage = "Failed to load USD device";
    tsd::core::logError("%s", m_statusMessage.c_str());
    return;
  }

  anari::retain(m_device, m_device);

  auto &scene = appCore()->tsd.scene;
  if (!prepareUsdExportRenderResources(
          adm, scene, m_device, m_renderIndex, m_usdRendererIndex))
  {
    m_statusMessage = "USD device: no renderer subtype available";
    tsd::core::logError("%s", m_statusMessage.c_str());
    anari::release(m_device, m_device);
    m_device = nullptr;
    return;
  }

  // Forward animation time changes to the USD device's usd::time parameter
  auto dev = m_device;
  m_renderIndex->setAnimationTimeCallback(
      [dev](float time) {
        double usdTime = static_cast<double>(time);
        anari::setParameter(dev, dev, "usd::time", usdTime);
        anari::commitParameters(dev, dev);
      });

  m_deviceReady = true;
  m_statusMessage = "USD device ready";
  tsd::core::logStatus("%s", m_statusMessage.c_str());

  // Build initial output URL for reference
  if (!m_hostName.empty())
  {
    if (m_hostName.find("://") != std::string::npos)
      m_lastOutputUrl = m_hostName + "/" + m_location;
    else
      m_lastOutputUrl = "omniverse://" + m_hostName + "/" + m_location;
  }
  else
  {
    m_lastOutputUrl = m_location;
  }
}

void UsdDevicePanel::teardownDevice()
{
  if (!m_deviceReady && !m_device)
    return;

  tsd::core::logStatus("tearing down USD device...");

  if (m_device)
  {
    releaseUsdExportRenderResources(appCore()->anari, m_device, m_renderIndex);
    anari::release(m_device, m_device);
    m_device = nullptr;
  }

  m_deviceReady = false;
  m_statusMessage.clear();
}

void UsdDevicePanel::applyMutableDeviceParams()
{
  if (!m_device)
    return;

  anari::setParameter(
      m_device, m_device, "usd::enableSaving", m_enableSaving);

  anari::commitParameters(m_device, m_device);
}

void UsdDevicePanel::setTimeOnDevice(float time)
{
  if (!m_device)
    return;

  double usdTime = static_cast<double>(time);
  anari::setParameter(m_device, m_device, "usd::time", usdTime);
  anari::commitParameters(m_device, m_device);
}

void UsdDevicePanel::ui_connectionSettings()
{
  ImGui::Text("Connection Settings");
  ImGui::Indent();

  ImGui::BeginDisabled(m_deviceReady);

  char hostBuf[256] = {};
  std::strncpy(hostBuf, m_hostName.c_str(), sizeof(hostBuf) - 1);
  ImGui::InputText("Server URL", hostBuf, sizeof(hostBuf));
  m_hostName = hostBuf;
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    ImGui::SetTooltip(
        "Omniverse Nucleus server name, or the https URL\n"
        "of the storage backend (e.g. S3/Azure bucket) when\n"
        "using OmniStorage. Leave empty for local output.\n"
        "Immutable after device creation.");
  }

  char locBuf[512] = {};
  std::strncpy(locBuf, m_location.c_str(), sizeof(locBuf) - 1);
  ImGui::InputText("Output Folder", locBuf, sizeof(locBuf));
  m_location = locBuf;
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    ImGui::SetTooltip(
        "Optional subfolder within the server URL or local\n"
        "disk to which USD files will be written.\n"
        "Immutable after device creation.");
  }

  ImGui::EndDisabled();

  ImGui::Unindent();
}

void UsdDevicePanel::ui_outputSettings()
{
  ImGui::Text("Output Settings");
  ImGui::Indent();

  ImGui::BeginDisabled(m_deviceReady);

  ImGui::Checkbox("New Session", &m_newSession);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    ImGui::SetTooltip("Create a new session directory (Session_N) vs reuse.\n"
        "Immutable after device creation.");

  ImGui::Checkbox("Binary USD (.usd)", &m_outputBinary);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    ImGui::SetTooltip("Binary .usd vs ASCII .usda output.\n"
        "Immutable after device creation.");

  ImGui::Checkbox("Output Materials", &m_outputMaterials);
  ImGui::Checkbox("Preview Surface Shader", &m_outputPreviewSurface);
  ImGui::Checkbox("MDL Shader", &m_outputMdlShader);

  ImGui::EndDisabled();

  if (ImGui::Checkbox("Enable Saving", &m_enableSaving))
    applyMutableDeviceParams();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    ImGui::SetTooltip("Write USD to disk. Disable for in-memory only.\n"
        "Can be changed while the device is active.");

  ImGui::Separator();
  ImGui::Text("Render product resolution");
  if (ImGui::Checkbox("Match main viewport", &m_exportFrames->matchViewport))
  {
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    ImGui::SetTooltip(
        "Use the main Viewport window's render size (after resolution scale)\n"
        "for each USD RenderProduct. Otherwise use Width/Height below.");
  }

  ImGui::BeginDisabled(m_exportFrames->matchViewport);
  int ew = static_cast<int>(m_exportFrames->width);
  int eh = static_cast<int>(m_exportFrames->height);
  if (ImGui::InputInt("Width", &ew))
    m_exportFrames->width = static_cast<uint32_t>(std::max(1, ew));
  if (ImGui::InputInt("Height", &eh))
    m_exportFrames->height = static_cast<uint32_t>(std::max(1, eh));
  ImGui::EndDisabled();

  if (m_exportFrames->matchViewport && m_exportFrames->mainViewport != nullptr)
  {
    const auto rs = m_exportFrames->mainViewport->renderPixelSize();
    ImGui::TextDisabled("Viewport render: %d x %d", rs.x, rs.y);
  }

  ImGui::Unindent();
}

void UsdDevicePanel::ui_controls()
{
  ImGui::Text("Controls");
  ImGui::Indent();

  if (!m_deviceReady)
  {
    if (ImGui::Button("Enable USD Device"))
      setupDevice();
  }
  else
  {
    if (ImGui::Button("Sync Scene"))
      syncScene();

    ImGui::SameLine();
    if (ImGui::Button("Disable"))
      teardownDevice();

    ImGui::Checkbox("Auto-sync on scene change", &m_autoSync);
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Automatically call renderFrame on the USD device\n"
          "when the scene changes. This keeps the USD output\n"
          "continuously in sync with the TSD scene.");
    }

    if (m_autoSync && m_deviceReady)
    {
      auto &sc = appCore()->tsd.scene;
      setTimeOnDevice(sc.getAnimationTime());
      synchronizeUsdDeviceScene(sc,
          m_device,
          *m_renderIndex,
          m_usdRendererIndex,
          m_exportFrames->resolvedSize());
    }

    ImGui::Separator();

    auto &scene = appCore()->tsd.scene;
    float currentTime = scene.getAnimationTime();

    ImGui::Text("Current Time: %.3f", currentTime);

    if (ImGui::Button("Sync at Current Time"))
    {
      setTimeOnDevice(currentTime);
      syncScene();
    }
  }

  ImGui::Unindent();
}

void UsdDevicePanel::ui_status()
{
  ImGui::Text("Status");
  ImGui::Indent();

  if (m_deviceReady)
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.f), "Device Active");
  else
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.f), "Device Inactive");

  if (!m_statusMessage.empty())
    ImGui::TextWrapped("%s", m_statusMessage.c_str());

  if (!m_lastOutputUrl.empty())
  {
    ImGui::Text("Output: %s", m_lastOutputUrl.c_str());

    if (ImGui::Button("Copy URL"))
      ImGui::SetClipboardText(m_lastOutputUrl.c_str());
  }

  ImGui::Unindent();
}

} // namespace tsd_usd
