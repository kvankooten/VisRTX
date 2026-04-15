// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#include "UsdDeviceSceneSync.hpp"
#include "tsd/core/Logging.hpp"
// std
#include <cctype>
#include <string>
#include <unordered_set>

namespace {

std::string sanitizeForUsdExportFrameToken(const std::string &raw)
{
  std::string out;
  out.reserve(raw.size());
  for (unsigned char uc : raw)
  {
    char c = static_cast<char>(uc);
    if (std::isalnum(static_cast<unsigned char>(c)) != 0)
      out += c;
    else if (c == '_' || c == '-' || c == '.')
      out += '_';
    else if (std::isspace(static_cast<unsigned char>(c)) != 0)
    {
      if (!out.empty() && out.back() != '_')
        out += '_';
    }
    else if (!out.empty() && out.back() != '_')
      out += '_';
  }
  while (!out.empty() && out.back() == '_')
    out.pop_back();
  return out;
}

std::string uniqueUsdExportFrameName(const std::string &rawName,
    size_t cameraIndex,
    std::unordered_set<std::string> &used)
{
  std::string token = sanitizeForUsdExportFrameToken(rawName);
  if (token.empty())
    token = "cam_" + std::to_string(cameraIndex);

  const std::string base = "usdExport_" + token;
  std::string candidate = base;
  unsigned n = 0;
  while (used.count(candidate) != 0)
  {
    candidate = base + "_" + std::to_string(n);
    ++n;
  }
  used.insert(candidate);
  return candidate;
}

} // namespace

namespace tsd::usd_export {

void configureUsdExportFrame(anari::Device device,
    anari::Frame frame,
    tsd::rendering::RenderIndex &renderIndex,
    anari::Renderer renderer,
    tsd::math::uint2 size)
{
  anari::setParameter(device, frame, "world", renderIndex.world());
  anari::setParameter(device, frame, "size", size);
  anari::setParameter(device, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(device, frame, "channel.depth", ANARI_FLOAT32);
  anari::setParameter(device, frame, "accumulation", false);
  anari::setParameter(device, frame, "renderer", renderer);
  anari::commitParameters(device, frame);
}

bool prepareUsdExportRenderResources(tsd::app::ANARIDeviceManager &adm,
    tsd::core::Scene &scene,
    anari::Device device,
    tsd::rendering::RenderIndex *&renderIndexOut,
    size_t &rendererIndexOut)
{
  renderIndexOut = adm.acquireRenderIndex(scene, "usd", device);

  auto renderers = scene.renderersOfDevice("usd");
  if (renderers.empty())
    renderers = scene.createStandardRenderers("usd", device);
  if (renderers.empty())
  {
    tsd::core::logError(
        "[USD export] no renderer subtype available for USD device");
    adm.releaseRenderIndex(device);
    renderIndexOut = nullptr;
    return false;
  }

  rendererIndexOut = renderers[0]->index();
  return true;
}

void releaseUsdExportRenderResources(tsd::app::ANARIDeviceManager &adm,
    anari::Device device,
    tsd::rendering::RenderIndex *&renderIndex)
{
  if (renderIndex)
  {
    adm.releaseRenderIndex(device);
    renderIndex = nullptr;
  }
}

static void renderOneUsdExportPass(anari::Device device,
    tsd::rendering::RenderIndex &renderIndex,
    anari::Renderer renderer,
    tsd::math::uint2 size,
    const char *frameName,
    anari::Camera cameraOrNull,
    float cameraAspect)
{
  anari::Frame f = anari::newObject<anari::Frame>(device);
  anari::setParameter(device, f, "name", frameName);
  configureUsdExportFrame(device, f, renderIndex, renderer, size);

  if (cameraOrNull)
  {
    anari::setParameter(device, cameraOrNull, "aspect", cameraAspect);
    anari::commitParameters(device, cameraOrNull);
    anari::setParameter(device, f, "camera", cameraOrNull);
  }
  else
    anari::unsetParameter(device, f, "camera");

  anari::commitParameters(device, f);
  anari::render(device, f);
  anari::wait(device, f);
  anari::release(device, f);
}

void synchronizeUsdDeviceScene(tsd::core::Scene &scene,
    anari::Device device,
    tsd::rendering::RenderIndex &renderIndex,
    size_t rendererObjectIndex,
    tsd::math::uint2 size)
{
  anari::Renderer r = renderIndex.renderer(rendererObjectIndex);
  if (!r)
  {
    tsd::core::logWarning(
        "[USD export] missing renderer at index %zu", rendererObjectIndex);
    return;
  }

  const size_t nCams = scene.numberOfObjects(ANARI_CAMERA);
  const float aspect = size.x / float(size.y);

  if (nCams == 0)
  {
    renderOneUsdExportPass(
        device, renderIndex, r, size, "usdExport_world", nullptr, aspect);
    return;
  }

  std::unordered_set<std::string> usedFrameNames;
  for (size_t i = 0; i < nCams; ++i)
  {
    anari::Camera cam = renderIndex.camera(i);
    if (!cam)
      continue;

    auto camObj = scene.getObject<tsd::core::Camera>(i);
    const std::string rawName =
        camObj.data() ? std::string(camObj->name()) : std::string{};
    const std::string fname =
        uniqueUsdExportFrameName(rawName, i, usedFrameNames);

    renderOneUsdExportPass(
        device, renderIndex, r, size, fname.c_str(), cam, aspect);
  }
}

} // namespace tsd::usd_export
