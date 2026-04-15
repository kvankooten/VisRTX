// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

#include "UsdExportFrameSettings.hpp"
#include "tsd/ui/imgui/windows/BaseViewport.h"
// std
#include <algorithm>

namespace tsd_usd {

tsd::math::uint2 UsdExportFrameSettings::resolvedSize() const
{
  if (matchViewport && mainViewport != nullptr)
  {
    const auto s = mainViewport->renderPixelSize();
    if (s.x > 0 && s.y > 0)
      return {static_cast<uint32_t>(s.x), static_cast<uint32_t>(s.y)};
  }
  return {std::max(1u, width), std::max(1u, height)};
}

} // namespace tsd_usd
