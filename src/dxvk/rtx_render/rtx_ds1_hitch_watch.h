#pragma once

#include "rtx_option.h"

namespace dxvk {

  // DS1: what the runtime was doing while a frame was late.
  //
  // The [DS1 hitch] line says a frame came late; it cannot say why, since the thread that would log it is the one that is
  // stuck. A watchdog thread looks at the time since the last frame came through (RtxContext::injectRTX); once that passes
  // rtx.ds1Dev.hitchStackMs it takes the call stacks of the two threads a frame depends on, every 50 ms until the frame
  // arrives: the render thread (dxvk-cs, which runs the draws and the path tracer) and the API thread (which takes the game's
  // D3D9 calls from the bridge). When the frame arrives the stacks are written to the log, equal ones counted together:
  //
  //   [DS1 hitch stacks] 842 ms without a frame, 15 samples
  //     render thread 12x: dxvk::RtxTextureManager::... < dxvk::SceneManager::... < ...
  //     API thread 15x: dxvk::D3D9CommonTexture::... < ...
  //
  // Dev mode only (rtx.ds1Dev.enable): a player's game starts no watchdog.
  class Ds1HitchWatch {
  public:
    RTX_OPTION("rtx.ds1Dev", int, hitchStackMs, 50,
               "Dev mode: a frame later than this many ms has the call stacks of the render and API threads logged ([DS1 hitch stacks]). 0 turns it off.");

    // From D3D9SwapChainEx::Present: the calling thread is the API thread.
    static void onApiPresent();
    // From RtxContext::injectRTX: the calling thread is the render thread, and a frame came through.
    static void onFrame();
  };

}
