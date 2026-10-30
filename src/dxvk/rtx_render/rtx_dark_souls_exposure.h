#pragma once

#include "rtx_option.h"

#include <cmath>

namespace dxvk {

  // Dark Souls 1 exposure: the game's own, applied to Remix's image.
  //
  // The game meters the log-average luminance of its frame (FRPG_Fil_SampleLum*),
  // adapts toward key / clamp(average, min, max) at its own speed with the area's
  // ToneMapBank values (FRPG_Fil_CalcAdaptedLum), and every lit shader then ends
  // with colour * c135.x / c135.y. That per-area, narrowly clamped exposure is why
  // dark areas stay dark.
  //
  // The game lights in gamma space (no sRGB read or write on any lit draw): the
  // texture's stored value times the light times the exposure, saturated. The DS1
  // look (rtx_dark_souls_look.h) applies that exposure in its scene pass; a
  // multiplier E on gamma-space colour is E^2.2 on linear radiance, which the
  // passes that still expose linear radiance use (unlit sprites' compensation,
  // DLSS-NR's fast tonemap). With the look off nothing here applies: Remix meters
  // and tonemaps its own image.
  class DxvkDarkSoulsExposure {
  public:
    // True when the game's exposure replaces Remix's auto exposure this frame: while the DS1 look runs.
    static bool replacesAutoExposure();

    // Multiplier on top of rtx.tonemap.exposureBias: the game's exposure on linear radiance, or 1.
    static float factor() { return replacesAutoExposure() ? linearExposure() : 1.0f; }

    // The game's exposure as a multiplier on linear radiance.
    static float linearExposure() { return std::pow(std::max(gameExposureValue(), 0.0f), 2.2f); }

    // The game's exposure (c135.x / c135.y): metered on Remix's frame when that runs, else the
    // game's last value, 1 before the proxy sent one.
    static float gameExposureValue() {
      if (metering()) {
        return s_adapted / std::max(exposureDivisor(), 1e-3f);
      }
      return gameActive() ? gameExposure() : 1.0f;
    }

    // The exposure the game's own shaders applied to what they drew (its c135.x / c135.y), which the
    // sky raster holds and must be un-exposed by. Not gameExposureValue: when Remix meters its frame
    // the two differ, and in the real game they differ a lot, since with SkipPostFX the game's own
    // luminance passes are dropped and its c135 stays at its boot value (1 / 1.3) while the meter
    // follows the scene (Ash Lake: sky un-exposed by 1.38 instead of 0.77, about 0.28x too dark).
    static float drawnExposure() { return gameActive() ? gameExposure() : 1.0f; }

    // Metering: the game's own luminance meter (FRPG_Fil_SampleLumInitial / SampleLumFinal) runs on
    // the frame Remix shows instead of the game's buffer, which under path tracing is not that
    // image, and FRPG_Fil_CalcAdaptedLum adapts the game's exposure toward it:
    //   E += (1 - 2^(-0.874390364 * c54.x)) * (c54.w / (clamp(lum, c54.y, c54.z) + 0.001) - E)
    // once per frame, and c135 = (E, c135.y). True once a metered value exists.
    static bool metering() { return meterFrame() && adaptActive() && s_haveAdapted; }

    // True when the look should meter its frame this frame.
    static bool wantsMeter() { return meterFrame() && adaptActive(); }

    // One frame's adaptation step from the frame's geometric-mean luminance (the game's lum).
    // ambientScale: the share of the game's unoccluded ambient the frame keeps (the look's ambient
    // meter), which the game's luminance clamp is scaled by under ambientClamp.
    static void adapt(float lum, float ambientScale);

    static void showImguiSettings();

    // Game state, set by the proxy. Never saved.
    RTX_OPTION_FLAG("rtx.ds1Exposure", bool, gameActive, false, RtxOptionFlags::NoSave,
                    "Set by the DS1 proxy once it has read the game's exposure.");
    RTX_OPTION_FLAG("rtx.ds1Exposure", float, gameExposure, 1.0f, RtxOptionFlags::NoSave,
                    "The game's exposure, c135.x / c135.y.");

    RTX_OPTION("rtx.ds1Exposure", bool, meterFrame, true,
               "Meter the frame Remix shows with the game's own luminance meter and adaptation (its ToneMapBank "
               "values, PS c54) instead of taking the game's exposure, which the game meters on its own buffer.");
    RTX_OPTION_FLAG("rtx.ds1Exposure", bool, adaptActive, false, RtxOptionFlags::NoSave,
                    "Set by the DS1 proxy once it has read the game's adaptation constants.");
    RTX_OPTION_FLAG("rtx.ds1Exposure", float, adaptRate, 0.05f, RtxOptionFlags::NoSave, "Adaptation rate (PS c54.x).");
    RTX_OPTION_FLAG("rtx.ds1Exposure", float, adaptMinLum, 0.1f, RtxOptionFlags::NoSave, "Luminance clamp minimum (PS c54.y).");
    RTX_OPTION("rtx.ds1Exposure", float, minLumScale, 1.0f,
               "Scale on the game's luminance clamp minimum while metering Remix's frame: below 1 the exposure can rise "
               "past the game's limit where the path-traced frame is darker than the game's baked one.");
    RTX_OPTION_FLAG("rtx.ds1Exposure", float, adaptMaxLum, 0.15f, RtxOptionFlags::NoSave, "Luminance clamp maximum (PS c54.z).");
    RTX_OPTION("rtx.ds1Exposure", bool, ambientClamp, false,
               "Scale the game's luminance clamp by the share of the game's ambient the path-traced frame keeps. The game "
               "lights surfaces with an unoccluded ambient probe and sets each area's clamp for frames lit that way; path "
               "tracing occludes that ambient, so enclosed areas (Lost Izalith, interiors) meter far below the clamp, the "
               "exposure stops at the game's limit, and the ColAdj black offset then clips most of the frame to black. "
               "The share is measured each frame: the frame's log-average luminance against the game's ambient cube on the "
               "same surfaces (albedo * ambient(N)), never above 1, so open areas keep the game's clamp.");
    RTX_OPTION("rtx.ds1Exposure", float, ambientClampMinScale, 0.1f,
               "Lower bound on the measured ambient share (a safety bound for degenerate frames, such as a wall filling "
               "the view).");
    RTX_OPTION_FLAG("rtx.ds1Exposure", float, adaptKey, 0.19f, RtxOptionFlags::NoSave, "Key (PS c54.w).");
    RTX_OPTION_FLAG("rtx.ds1Exposure", float, exposureDivisor, 1.3f, RtxOptionFlags::NoSave,
                    "c135.y: the lit shaders scale by c135.x / c135.y.");

  private:
    inline static float s_adapted = 1.0f;   // E, the game's c135.x
    inline static bool s_haveAdapted = false;
    inline static float s_ambientScale = 1.0f;
    inline static float s_meteredLum = 0.0f;
  };

} // namespace dxvk
