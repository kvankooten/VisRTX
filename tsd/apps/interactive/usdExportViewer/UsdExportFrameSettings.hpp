// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#pragma once

// tsd_core
#include "tsd/core/TSDMath.hpp"
// std
#include <cstdint>

namespace tsd::ui::imgui {
struct BaseViewport;
}

namespace tsd_usd {

// Resolution used for ANARI Frame `size` when syncing to the USD device
// (RenderProduct resolution in AnariUsdDevice). Shared by UsdDevicePanel and
// Application (Tools -> Sync) when a pointer is passed from tsdUsdExportViewer.
struct UsdExportFrameSettings
{
  tsd::ui::imgui::BaseViewport *mainViewport{nullptr};
  uint32_t width{1280};
  uint32_t height{720};
  bool matchViewport{false};

  tsd::math::uint2 resolvedSize() const;
};

} // namespace tsd_usd
