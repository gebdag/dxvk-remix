// The rtx.legacyMaterial specular controls (specularLevel / fresnelF90).
//
// Why these controls exist
// ------------------------
// Remix's opaque BSDF adds a GGX specular lobe on top of a full strength
// diffuse lobe. The Hammon diffuse model compensates for Fresnel transmission
// through its smooth-surface term, but that term is lerped away as roughness
// approaches 1, so at high roughness the specular energy is purely additive.
// Compounding that, evalOpaqueSchlickFresnel hardcodes f90 to 1.0, making every
// surface a perfect white mirror at grazing angles. With a broad (rough) GGX
// lobe that grazing energy is smeared across the whole hemisphere, producing an
// achromatic veil that scales with scene irradiance and is independent of the
// albedo texture - the "haze" that game-original materials show at roughness 1.
//
// specularLevel scales the dielectric F0 (folded into the base reflectivity at
// interaction creation, so it needs no extra GBuffer storage); fresnelF90 lowers
// the Schlick ceiling and is applied at every Fresnel evaluation for the
// interaction. Both are neutral at 1.0, which reproduces stock Remix exactly.
//
// No private-member access; both hooks are pure policy.

#include "rtx_legacy_specular.h"

#include "rtx_materials.h"                  // LegacyMaterialDefaults, kSurfaceMaterialInvalidTextureIndex
#include "rtx/pass/material_args.h"         // OpaqueMaterialArgs

#include <algorithm>

namespace dxvk {

  // ---------------------------------------------------------------------------
  // useLegacySpecularOverrides
  //
  // Decides whether a given opaque material is eligible for the legacy specular
  // controls. The rule is "nobody made a deliberate roughness decision for this
  // material":
  //
  //   * Materials synthesized from the game's own fixed function state carry
  //     OpaqueMaterialData::IsLegacyMaterial (set in
  //     LegacyMaterialData::createDefault). They are always eligible - note that
  //     createDefault assigns the roughness constant through the setter, which
  //     marks it dirty, so the authored-roughness test below would otherwise
  //     exclude exactly the materials these controls were written for.
  //
  //   * Replacement materials are eligible only when they author neither a
  //     roughness constant nor a roughness map. An unauthored roughness falls
  //     back to OpaqueMaterialData's compiled-in 0.5 default, which is not a
  //     considered value; a replacement that does specify roughness is treated
  //     as an intentional PBR authoring choice and keeps the stock response.
  //
  // isRoughnessConstantModified() reads the per-parameter dirty bit that
  // OpaqueMaterialData already maintains for merge(): set by any setter and by
  // the USD deserializer when the attribute is actually present on the prim.
  //
  // roughnessTextureIndex is the resolved binding index rather than the
  // TextureRef, so an authored-but-empty texture path (or a material whose
  // textures were skipped, e.g. white material mode) reads as "no map".
  // ---------------------------------------------------------------------------
  bool useLegacySpecularOverrides(const OpaqueMaterialData& opaqueMaterialData,
                                  const uint32_t roughnessTextureIndex) {
    if (opaqueMaterialData.getIsLegacyMaterial()) {
      return true;
    }

    const bool roughnessAuthored =
      opaqueMaterialData.isRoughnessConstantModified() ||
      roughnessTextureIndex != kSurfaceMaterialInvalidTextureIndex;

    return !roughnessAuthored;
  }

  // ---------------------------------------------------------------------------
  // fillLegacySpecularArgs
  //
  // Publishes the two controls into the per-frame opaque material constant
  // buffer. They ride the constant buffer rather than the material payload so
  // that dragging the sliders costs nothing - no cached material is invalidated
  // and nothing is rebuilt.
  //
  // Called from RtxContext::updateRaytraceArgs rather than from
  // OpaqueMaterialOptions::fillShaderParams because the options live on
  // LegacyMaterialDefaults, which fillShaderParams cannot see.
  // ---------------------------------------------------------------------------
  void fillLegacySpecularArgs(OpaqueMaterialArgs& args) {
    args.legacySpecularLevel = std::clamp(LegacyMaterialDefaults::specularLevel(), 0.0f, 1.0f);
    args.legacyFresnelF90 = std::clamp(LegacyMaterialDefaults::fresnelF90(), 0.0f, 1.0f);
  }

} // namespace dxvk
