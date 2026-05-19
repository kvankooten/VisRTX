// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#include "SrtxControlPanel.h"
// local
#include "SrtxViewport.h"
// tsd_ui_imgui
#include "imgui.h"
#include "tsd/ui/imgui/Application.h"
// std
#include <algorithm>
#include <cstring>

namespace tsd_srtx {

SrtxControlPanel::SrtxControlPanel(tsd::ui::imgui::Application *app,
    SrtxViewport *viewport,
    const char *name)
    : Window(app, name)
    , m_viewport(viewport)
{}

void SrtxControlPanel::buildUI()
{
  if (!m_viewport)
  {
    ImGui::TextDisabled("No SRTX viewport bound.");
    return;
  }

  ui_connectionSettings();
  ImGui::Separator();
  ui_resolution();
  ImGui::Separator();
  ui_camera();
  ImGui::Separator();
  ui_controls();
  ImGui::Separator();
  ui_frameCapture();
  ImGui::Separator();
  ui_status();
}

void SrtxControlPanel::ui_connectionSettings()
{
  ImGui::Text("Connection Settings");
  ImGui::Indent();

  // The connection settings are immutable while the device is live to match
  // the UsdDevicePanel pattern -- the user disconnects, edits, reconnects.
  ImGui::BeginDisabled(m_viewport->isConnected());

  char serverBuf[256] = {};
  std::strncpy(
      serverBuf, m_viewport->serverUrl().c_str(), sizeof(serverBuf) - 1);
  if (ImGui::InputText("Server URL", serverBuf, sizeof(serverBuf)))
    m_viewport->setServerUrl(serverBuf);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    ImGui::SetTooltip(
        "host:port of the SRTX render server\n"
        "(e.g. localhost:50051).");
  }

  char stageBuf[512] = {};
  std::strncpy(stageBuf, m_viewport->stageUrl().c_str(), sizeof(stageBuf) - 1);
  if (ImGui::InputText("Stage URL", stageBuf, sizeof(stageBuf)))
    m_viewport->setStageUrl(stageBuf);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    ImGui::SetTooltip(
        "Path of the USD stage the server should open\n"
        "(local path or s3:// URL).");
  }

  char productBuf[256] = {};
  std::strncpy(productBuf,
      m_viewport->renderProductPath().c_str(),
      sizeof(productBuf) - 1);
  if (ImGui::InputText("Render Product Path", productBuf, sizeof(productBuf)))
    m_viewport->setRenderProductPath(productBuf);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    ImGui::SetTooltip(
        "USD prim path of the UsdRenderProduct to render.\n"
        "The server resolves the bound camera and resolution\n"
        "from this product.");
  }

  ImGui::EndDisabled();

  ImGui::Unindent();
}

void SrtxControlPanel::ui_resolution()
{
  ImGui::Text("Render Resolution");
  ImGui::Indent();

  static const char *kResolutionModeLabels[] = {
      "USD default", "Custom", "Match viewport"};
  int modeIndex = static_cast<int>(m_viewport->resolutionMode());
  if (ImGui::Combo("Mode",
          &modeIndex,
          kResolutionModeLabels,
          IM_ARRAYSIZE(kResolutionModeLabels)))
  {
    m_viewport->setResolutionMode(
        static_cast<SrtxViewport::ResolutionMode>(modeIndex));
  }

  switch (m_viewport->resolutionMode())
  {
  case SrtxViewport::ResolutionMode::Custom:
  {
    tsd::math::int2 custom = m_viewport->customResolution();
    int wh[2] = {custom.x, custom.y};
    if (ImGui::DragInt2("Width / Height", wh, 1.f, 1, 16384))
      m_viewport->setCustomResolution(tsd::math::int2(wh[0], wh[1]));
    break;
  }
  case SrtxViewport::ResolutionMode::MatchViewport:
  {
    const auto pending = m_viewport->pendingMatchSize();
    ImGui::TextDisabled(
        "Tracking dock area (debounced);\ncurrent target: %i x %i",
        pending.x,
        pending.y);
    break;
  }
  case SrtxViewport::ResolutionMode::UsdDefault:
  default:
    ImGui::TextDisabled("Using the resolution defined in the USD stage.");
    break;
  }

  ImGui::Unindent();
}

void SrtxControlPanel::ui_camera()
{
  ImGui::Text("Camera Navigation");
  ImGui::Indent();

  char camBuf[256] = {};
  std::strncpy(camBuf, m_viewport->cameraPath().c_str(), sizeof(camBuf) - 1);
  if (ImGui::InputText("Camera Path", camBuf, sizeof(camBuf)))
    m_viewport->setCameraPath(camBuf);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    ImGui::SetTooltip(
        "USD prim path of the camera fly-through navigation drives.\n"
        "Leave empty to use the camera defined in the USD stage.\n"
        "Hold the right mouse button over the viewport to fly:\n"
        "  mouse  - look around\n"
        "  W/A/S/D - move (forward/left/back/right)\n"
        "  E/Q     - move up/down (along world Y)\n"
        "  Shift   - boost, Ctrl - slow");
  }

  float speed = m_viewport->flySpeed();
  if (ImGui::DragFloat(
          "Fly Speed", &speed, 0.05f, 0.f, 1000.f, "%.2f units/s"))
    m_viewport->setFlySpeed(speed);

  float sens = m_viewport->lookSensitivity();
  if (ImGui::DragFloat(
          "Look Sensitivity", &sens, 0.0005f, 0.f, 0.1f, "%.4f rad/px"))
    m_viewport->setLookSensitivity(sens);

  ImGui::BeginDisabled(
      m_viewport->cameraPath().empty() || !m_viewport->isConnected());
  if (ImGui::Button("Apply Camera Path"))
    m_viewport->applyCameraPath();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    ImGui::SetTooltip(
        "Re-read the worldMatrix of the configured camera path and use that\n"
        "as the starting pose for further navigation. Use this after editing\n"
        "the camera-path text field to rebind the manipulator to that prim.");
  }

  ImGui::Unindent();
}

void SrtxControlPanel::ui_controls()
{
  ImGui::Text("Device");
  ImGui::Indent();

  ImGui::BeginDisabled(!m_viewport->canConnect());
  if (ImGui::Button("Connect & Render"))
    m_viewport->connect();
  ImGui::EndDisabled();

  ImGui::SameLine();

  ImGui::BeginDisabled(!m_viewport->isConnected());
  if (ImGui::Button("Disconnect"))
    m_viewport->disconnect();
  ImGui::EndDisabled();

  ImGui::Unindent();
}

void SrtxControlPanel::ui_frameCapture()
{
  ImGui::Text("Frame Capture");
  ImGui::Indent();

  const bool canSave =
      m_viewport->isConnected() && m_viewport->hasRenderedFrame();
  ImGui::BeginDisabled(!canSave);
  if (ImGui::Button("Save Frame as PNG"))
    m_viewport->requestSaveFrame();
  ImGui::EndDisabled();

  if (m_viewport->hasRenderedFrame())
  {
    ImGui::SameLine();
    ImGui::TextDisabled("(%u x %u)",
        m_viewport->lastFrameWidth(),
        m_viewport->lastFrameHeight());
  }

  ImGui::Unindent();
}

void SrtxControlPanel::ui_status()
{
  ImGui::Text("Status");
  ImGui::Indent();
  if (m_viewport->isConnected())
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.f), "Connected");
  else
    ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.f), "Disconnected");

  const std::string &msg = m_viewport->statusMessage();
  if (!msg.empty())
    ImGui::TextWrapped("%s", msg.c_str());
  ImGui::Unindent();
}

} // namespace tsd_srtx
