#pragma once

#include "rtx_options.h"

namespace dxvk {
  // Fork (DS1): the frame being path traced is a menu's character view (D3D9Rtx injects it into the proxy's private target,
  // sampler 15 BORDERCOLOR 0xD7E05CE5). Its sky is black, as on a fresh boot: after quitting to the menu the last map's sky
  // stayed in the sky raster and probe and showed behind the characters. Set around that frame's injectRTX only.
  inline bool g_ds1MenuFrame = false;

  // RtxOptions::skyBrightness for this frame: 0 for a menu's character view.
  inline float ds1SkyBrightness() {
    return g_ds1MenuFrame ? 0.0f : RtxOptions::skyBrightness();
  }
}
