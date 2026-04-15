// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#pragma once

// tsd_app
#include "tsd/app/ANARIDeviceManager.h"
// tsd_core
#include "tsd/core/TSDMath.hpp"
#include "tsd/core/scene/Scene.hpp"
// tsd_rendering
#include "tsd/rendering/index/RenderIndex.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <cstddef>

namespace tsd::usd_export {

// Acquire USD render index and first compatible renderer. No ANARI Frame: the
// ANARI USD device registers one RenderProduct per distinct frame *name*, so
// each export pass allocates its own named Frame in synchronizeUsdDeviceScene.
bool prepareUsdExportRenderResources(tsd::app::ANARIDeviceManager &adm,
    tsd::core::Scene &scene,
    anari::Device device,
    tsd::rendering::RenderIndex *&renderIndexOut,
    size_t &rendererIndexOut);

void releaseUsdExportRenderResources(tsd::app::ANARIDeviceManager &adm,
    anari::Device device,
    tsd::rendering::RenderIndex *&renderIndex);

void configureUsdExportFrame(anari::Device device,
    anari::Frame frame,
    tsd::rendering::RenderIndex &renderIndex,
    anari::Renderer renderer,
    tsd::math::uint2 size);

void synchronizeUsdDeviceScene(tsd::core::Scene &scene,
    anari::Device device,
    tsd::rendering::RenderIndex &renderIndex,
    size_t rendererObjectIndex,
    tsd::math::uint2 size);

} // namespace tsd::usd_export
