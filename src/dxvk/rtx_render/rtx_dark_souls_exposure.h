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
  // texture's stored value times the light times the exposure, saturated. A
  // multiplier E on gamma-space colour is E^2.2 on linear radiance, so with
  // followGame Remix's tonemapper uses
  //   exposure = calibration * (c135.x / c135.y)^2.2
  // (set each frame by the DS1 proxy) instead of metering its own image, and
  // with gameCurve its tone curve is the game's saturate.
  class DxvkDarkSoulsExposure {
  public:
    // True when the game's exposure replaces Remix's auto exposure this frame.
    static bool replacesAutoExposure() { return followGame() && gameActive(); }

    // Multiplier on top of rtx.tonemap.exposureBias: calibration * game exposure, or 1.
    static float factor() { return replacesAutoExposure() ? calibration() * linearExposure() : 1.0f; }

    // The game's exposure as a multiplier on linear radiance.
    static float linearExposure() { return std::pow(std::max(gameExposureValue(), 0.0f), 2.2f); }

    // True when the tonemapper should end in the game's saturate instead of an operator.
    static bool replacesToneCurve() { return replacesAutoExposure() && gameCurve(); }

    // The game's exposure (c135.x / c135.y): metered on Remix's frame when that runs, else the
    // game's last value, 1 before the proxy sent one.
    static float gameExposureValue() {
      if (metering()) {
        return s_adapted / std::max(exposureDivisor(), 1e-3f);
      }
      return gameActive() ? gameExposure() : 1.0f;
    }

    // Metering: the game's own luminance meter (FRPG_Fil_SampleLumInitial / SampleLumFinal) runs on
    // the frame Remix shows instead of the game's buffer, which under path tracing is not that
    // image, and FRPG_Fil_CalcAdaptedLum adapts the game's exposure toward it:
    //   E += (1 - 2^(-0.874390364 * c54.x)) * (c54.w / (clamp(lum, c54.y, c54.z) + 0.001) - E)
    // once per frame, and c135 = (E, c135.y). True once a metered value exists.
    static bool metering() { return meterFrame() && adaptActive() && s_haveAdapted; }

    // True when the look should meter its frame this frame.
    static bool wantsMeter() { return meterFrame() && adaptActive(); }

    // One frame's adaptation step from the frame's geometric-mean luminance (the game's lum).
    static void adapt(float lum);

    static void showImguiSettings();

    RTX_OPTION("rtx.ds1Exposure", bool, followGame, true,
               "Use the game's own exposure (its per-area, adapted ToneMap value c135.x / c135.y, set each frame by the DS1 "
               "proxy) instead of Remix's auto exposure.");
    RTX_OPTION("rtx.ds1Exposure", float, calibration, 1.0f,
               "Scale from the game's exposure to Remix's: Remix exposure = calibration * game exposure^2.2. One value for "
               "the whole game.");
    RTX_OPTION("rtx.ds1Exposure", bool, gameCurve, true,
               "While following the game's exposure, tonemap with the game's curve, a plain saturate (operator None), "
               "instead of rtx.tonemap.tonemapOperator.");

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
    RTX_OPTION_FLAG("rtx.ds1Exposure", float, adaptMaxLum, 0.15f, RtxOptionFlags::NoSave, "Luminance clamp maximum (PS c54.z).");
    RTX_OPTION_FLAG("rtx.ds1Exposure", float, adaptKey, 0.19f, RtxOptionFlags::NoSave, "Key (PS c54.w).");
    RTX_OPTION_FLAG("rtx.ds1Exposure", float, exposureDivisor, 1.3f, RtxOptionFlags::NoSave,
                    "c135.y: the lit shaders scale by c135.x / c135.y.");

  private:
    inline static float s_adapted = 1.0f;   // E, the game's c135.x
    inline static bool s_haveAdapted = false;
    inline static float s_meteredLum = 0.0f;
  };

} // namespace dxvk
