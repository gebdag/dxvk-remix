#pragma once

#include "rtx_option.h"
#include "../dxvk_image.h"

namespace dxvk {

  class RtxContext;

  // Fork: occlusion queries against the ray traced scene.
  //
  // A game's occlusion query counts the samples of a draw that pass its depth test. Under Remix the
  // world is ray traced, never rasterized, so the game's depth buffer holds nothing and every query
  // passes in full. Dark Souls hides its sun flare with one (a 24x24 test quad at the sun), so the
  // flare shone through walls. While an occlusion query is active, the D3D9 device binds a depth
  // buffer filled from the last ray traced frame instead, and the query's draw is tested against it.
  class DxvkDs1OcclusionDepth {
  public:
    // Fills depth (D32, the size of the game's depth target) through viewZ (R32F, same size, storage
    // and sampled): the last frame's primary view depth, scaled to the target and turned into the
    // game's depth values (projZZ + projWZ / viewZ; D3D projection _33 and _43). Far where nothing
    // was hit, and everywhere when no frame has been ray traced yet.
    static void fill(RtxContext* ctx, const Rc<DxvkImage>& depth, const Rc<DxvkImage>& viewZ,
                     const Rc<DxvkImageView>& viewZView, float projZZ, float projWZ);

    RTX_OPTION("rtx", bool, occlusionQueriesUseRaytracedDepth, true,
               "Occlusion queries test their draws against the last ray traced frame's depth instead of the game's depth buffer, "
               "which holds no ray traced geometry (Dark Souls hides its sun flare behind walls with one).");
  };

} // namespace dxvk
