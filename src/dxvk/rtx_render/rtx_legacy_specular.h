#pragma once

#include <cstdint>

struct OpaqueMaterialArgs;

namespace dxvk {
  struct OpaqueMaterialData;

  // Whether an opaque material gets the rtx.legacyMaterial specular controls (specularLevel /
  // fresnelF90): true for materials nobody made a deliberate roughness decision for, i.e. anything
  // synthesized from the game's own fixed function state, plus any replacement that authors neither a
  // roughness constant nor a roughness map. Becomes OPAQUE_SURFACE_MATERIAL_FLAG_USE_LEGACY_SPECULAR.
  bool useLegacySpecularOverrides(const OpaqueMaterialData& opaqueMaterialData,
                                  const uint32_t roughnessTextureIndex);

  // Publishes rtx.legacyMaterial.specularLevel / fresnelF90 into the per-frame opaque material
  // constants, after OpaqueMaterialOptions::fillShaderParams (which cannot see LegacyMaterialDefaults).
  void fillLegacySpecularArgs(OpaqueMaterialArgs& args);
}
