# Compare DI shading between samples
_Exported on 3/29/2026 at 21:20:07 EDT from Cursor (2.6.22)_

---

**Cursor**


Reading the listed shader pairs to compare DI shading, helpers, compositing, and RAB bridges.




Checking visibility functions (`RAB_GetConservativeVisibility` vs `GetFinalVisibility`) and `INSTANCE_MASK` definitions, and verifying MinimalGISample compositing flags.




Here is a structured comparison of the files you listed, focused on what can change **HDR lighting scale** (2–4×) versus subtle or quality-only differences.

---

## 1. `DIShadeSamples.hlsl` vs `DI/ShadeSamples.hlsl`

| Topic | MinimalGISample | IntermediateSample |
|--------|-----------------|---------------------|
| **Includes / defines** | `#define RTXDI_ENABLE_PRESAMPLING 0`, includes local `ShadingHelpers.hlsli` after bridge, includes `Reservoir.hlsli` only. | No presampling define here; `../RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli`; optional ReGIR + NRD headers; `../ShadingHelpers.hlsli`. |
| **Dispatch model** | Compute only: `SV_DispatchThreadID` → `pixelPosition`. | Compute **or** raygen (`USE_RAY_QUERY`): thread id is `GlobalIndex`. |
| **Reservoir index** | `RTXDI_LoadDIReservoir(..., pixelPosition, ...)`. | `RTXDI_LoadDIReservoir(..., GlobalIndex, ...)` — **reservoir grid index**, not always equal to pixel when checkerboard is used. |
| **G-buffer pixel** | Same as dispatch: `RAB_GetGBufferSurface(pixelPosition, false)`. | `pixelPosition = RTXDI_ReservoirPosToPixelPos(GlobalIndex, params.activeCheckerboardField)` — **checkerboard mapping**. |
| **Validity guard** | Shading only if `RAB_IsSurfaceValid(surface) && RTXDI_IsValidDIReservoir(reservoir)`. | Shading if `RTXDI_IsValidDIReservoir(reservoir)` only (**no** `RAB_IsSurfaceValid` here). |
| **Light sample → shading** | In-shader: `solidAnglePdf > 0`, `dot(L, geoNormal) > 0`, `EvaluateBrdf`, `radiance = emissive × invPdf / solidAnglePdf`, **bool** `RAB_GetConservativeVisibility`, then `diffuse/specular = brdf × radiance`. | `ShadeSurfaceWithLightSample(..., enableVisibilityReuse: true, ...)` — **no** explicit `geoNormal` test inside that helper (see §3). |
| **Visibility type** | Binary: if occluded, `radiance = 0` and reservoir visibility stored as `0`. | `float3` throughput from `GetFinalVisibility` (when final visibility is on), `lightSample.radiance *= visibility`. |
| **Reservoir write** | On occlusion: `RTXDI_StoreVisibilityInDIReservoir` + `RTXDI_StoreDIReservoir` immediately. | `needToStore` from helper; `RTXDI_StoreDIReservoir` only when `needToStore`. |
| **After shading** | `DemodulateSpecular` then `StoreShadingOutput(pixelPosition, ..., true)`. | Luminance buffer `u_RestirLuminance` (gradient pass; zeroed when `reservoir.age > 0`); optional ReGIR cell visualization (scales **diffuse**); `DemodulateSpecular`; `StoreShadingOutput(GlobalIndex, pixelPosition, viewDepth, roughness, ..., lightDistance, true, enableDenoiserInputPacking)`. |

**High impact for brightness:** checkerboard **indexing** (reservoir vs pixel); **geoNormal gate** only in Minimal; **final visibility optional / float3** in Intermediate vs **always conservative bool** in Minimal; **ReGIR debug** scaling diffuse in Intermediate.

---

## 2. `ShadingHelpers.hlsli` (pair 3–4)

### `EvaluateBrdf`

- **Shared:** `demodulatedDiffuse = Lambert(surface.normal, -L)`; specular uses `GGX_times_NdotL(..., max(roughness, kMinRoughness), specularF0)`.
- **Difference:** Intermediate **forces `specular = 0` when `roughness == 0`**. Minimal **always** evaluates GGX specular (no roughness == 0 special case).

### `DemodulateSpecular`

- **Identical:** `specular / max(0.01, surfaceSpecularF0)`.

### `ShadeSurfaceWithLightSample` (Intermediate only, `#ifdef RTXDI_DIRESERVOIR_HLSLI`)

Minimal’s DI shade **does not** use this; Intermediate does. Behavior differences:

1. Early out if `solidAnglePdf <= 0` → `return false` (no shading).
2. **`g_Const.restirDI.shadingParams.enableFinalVisibility`:** if **false**, **no shadow ray** — radiance is **not** reduced by occlusion (can read much **brighter** than Minimal, which **always** traces `RAB_GetConservativeVisibility`).
3. Optional **visibility reuse** via `RTXDI_GetDIReservoirVisibility` (`reuseFinalVisibility`, max age/distance).
4. Otherwise `GetFinalVisibility(SceneBVH or PrevSceneBVH, ...)` → **`float3`** multiplied into `lightSample.radiance`.
5. `RTXDI_StoreVisibilityInDIReservoir(reservoir, visibility, discardInvisibleSamples)` with **float visibility**, not bool `0`.
6. **PDF weighting:** `lightSample.radiance *= RTXDI_GetDIReservoirInvPdf(reservoir) / lightSample.solidAnglePdf` **inside** the helper (same formula as Minimal, but applied after visibility).
7. **No** `dot(L, geoNormal) > 0` check — only `any(lightSample.radiance > 0)` after scaling.
8. Outputs `lightDistance = length(lightSample.position - surface.worldPos)`.

### `StoreShadingOutput`

| | MinimalGISample | IntermediateSample |
|---|-----------------|---------------------|
| **Signature** | `(pixelPosition, diffuse, specular, isFirstPass)` | `(reservoirPosition, pixelPosition, viewDepth, roughness, diffuse, specular, lightDistance, isFirstPass, isLastPass)` |
| **Write index** | Always `pixelPosition`. | If `denoiserMode != DENOISER_MODE_OFF`, uses **`reservoirPosition` (= `GlobalIndex`)**; else **`pixelPosition`**. |
| **W channel** | Always `0` for both targets. | Stores **`lightDistance`** (with rules below). |
| **Multi-pass accumulation** | Adds prior RGB at same index. | Adds prior RGB; **keeps prior `w` (hit distance)** if new lobe is dimmer or `lightDistance == 0` (luminance compare on diffuse/specular separately). |
| **Checkerboard / denoiser off** | **None.** | If `denoiserMode == OFF` **and** `activeCheckerboardField != 0` **and** `isLastPass`: can **`diffuse *= 2`, `specular *= 2`**, clear or copy neighbor field (depends on `DENOISER_MODE_RELAX` / `enableAccumulation`). **Strong candidate for ~2× vs a sample that never doubles.** |

MinimalGISample’s grep shows **no checkerboard** usage in the sample; Intermediate’s path is built for checkerboard + denoiser modes. If you ever enable similar reservoir layout in Minimal without this `×2` fix, brightness can drop on half the pixels or average lower.

---

## 3. `Compositing.hlsl` vs `CompositingPass.hlsl`

| Topic | MinimalGISample | IntermediateSample |
|--------|-----------------|---------------------|
| **Inputs** | `u_GBuffer*`, `u_DiffuseLighting`, `u_SpecularLighting`, `u_Emissive` at **`pixelPosition`**. | G-buffer textures + `t_Diffuse` / `t_Specular` (+ optional denoised); **miss** path: environment + motion vectors. |
| **Lighting fetch index** | Same as pixel. | If **denoiser on** and **checkerboard**: `illuminationPos.x = globalIdx.x / 2` (half-width lighting buffer stretched). |
| **Albedo / F0 recombine** | **Always** `color = diffuse * diffuseAlbedo + specular * max(0.01, specularF0) + emissive`. | **Only if `g_Const.enableTextures`:** same multiply; **else** `diffuse_illumination` / `specular_illumination` used **without** that multiply (assumes different buffer convention). |
| **NRD** | None in this file. | Unpack REBLUR, lerp noisy vs denoised with `noiseMix` / clamps. |
| **Tone mapping** | Optional **`basicToneMapping(color, 0.005)`** → lowers **displayed** HDR if enabled. | **No** tone mapping in this pass. |
| **Output** | `u_HdrColor`. | `u_Output`; NaN → blue debug. |

For apples-to-apples **linear HDR before tone map**, compare with **tone mapping off** in Minimal and **`enableTextures` true** in Intermediate.

---

## 4. `RAB_LightSampling.hlsli` (pair 7–8)

**Environment**

- Minimal: `RAB_GetEnvironmentMapRandXYFromDir` returns `(0,0)`; `RAB_EvaluateEnvironmentMapSamplingPdf` → `0`; `GetEnvironmentRadiance` → `0` (**no env** in this bridge).
- Intermediate: full env **UV**, PDF texture evaluation (importance sampling), `GetEnvironmentRadiance` from bindless env map × scale (when enabled).

**Local light PDF (`RAB_EvaluateLocalLightSourcePdf`)**

- Minimal: **uniform** `1.0 / numLights`.
- Intermediate: **importance** PDF from `t_LocalLightPdfTexture` (with mip/sum normalization).

This changes **resampling weights**, not the final shade formula by itself, but can change **which** reservoir sample survives (indirect brightness differences).

**`RAB_GetReflectedRadianceForSurface` / target PDFs**

- Minimal **`RAB_GetLightSampleTargetPdfForSurface`:** `calcLuminance(ShadeSurfaceWithLightSample(...))` where that helper returns `(radiance × (Lambert×albedo + GGX)) / solidAnglePdf` — includes **`ShadeSurfaceWithLightSample` in this file** (different from Intermediate’s `ShadingHelpers` function of the same name).
- Intermediate: `RAB_GetReflectedLuminanceForSurface(...) / lightSample.solidAnglePdf` with specular **zeroed when roughness == 0** (matches Intermediate reflected radiance path).

**Specular in reflected radiance**

- Minimal `RAB_GetReflectedRadianceForSurface`: always **GGX** spec term.
- Intermediate: **GGX only if roughness != 0**.

**`RAB_GetLightDirDistance`**

- Minimal: always from **position** vector.
- Intermediate: **environment** lights use `o_lightDir = -lightSample.normal`, `o_lightDistance = DISTANT_LIGHT_DISTANCE`.

**`IsComplexSurface`**

- Minimal: **always `true`**.
- Intermediate: compares **denoiser roughness** vs G-buffer roughness × permutation threshold (affects **resampling**, not this DI shade file directly).

**`RAB_TraceRayForLocalLight`**

- **Instance mask:** both use **`INSTANCE_MASK_OPAQUE`** for the inline `TraceRayInline` path.
- **RayQuery flags (inline):** both `RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES`, `RAY_FLAG_NONE` on `TraceRayInline`.
- **Difference:** Intermediate has a **`#else` path** using **`TraceRay`** with `RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES` and `RayPayload` (when `USE_RAY_QUERY` is 0). Minimal only shows the **RayQuery** path in this file.

**`RAB_SamplePolymorphicLight`**

- **Not in the same file in Intermediate** — it lives in `RAB_LightInfo.hlsli` and uses **`PolymorphicLight::calcSample`** (full polymorphic types + `lightType`).
- **Minimal:** same file, **`TriangleLight::Create` + `CalcSample`** only (triangle-oriented minimal path).

---

## 5. `RAB_Surface.hlsli` (pair 9–10)

| Topic | MinimalGISample | IntermediateSample |
|--------|-----------------|---------------------|
| **Struct layout** | `worldPos`, `viewDir`, `viewDepth`, `normal`, `geoNormal`, `diffuseProbability`, `material`. | `worldPos`, `viewDir`, `normal`, `geoNormal`, `viewDepth`, `diffuseProbability`, `material` (same fields, **different member order**). |
| **`kSpecularOnly`** | Not present. | `static const bool kSpecularOnly = false`. |
| **G-buffer read** | `RAB_GetGBufferSurface` reads **`u_GBufferDepth`**, **`u_GBufferNormals`**, **`u_GBufferGeoNormals`**, unpacks material from same buffers. | **`GetGBufferSurface`** reads **`t_GBufferDepth`**, normals/geo textures; material via **`RAB_GetGBufferMaterial(pixelPosition, previousFrame)`** (can differ from Minimal if that path applies different packing or derived channels). |
| **Previous frame** | Inline in `RAB_GetGBufferSurface` with `t_Prev*` and `g_Const.prevView`. | Delegates to `GetGBufferSurface` with `t_PrevGBuffer*`. |
| **`RAB_GetSurfaceBrdfSample` (specular branch)** | `ImportanceSampleGGX(rand.yz, roughness)` in tangent space, `reflect(-viewDir, ...)`. | **`ImportanceSampleGGX_VNDF`** in tangent space with **`worldToTangent(surface, viewDir)`** view vector — **different sampling** (GI / BRDF paths; not used by DI `EvaluateBrdf`). |
| **`RAB_GetSurfaceBrdfPdf`** | Mix of cosine hemisphere PDF and **`ImportanceSampleGGX_VNDF_PDF`**. | Diffuse PDF **0 if `kSpecularOnly`**; specular uses **VNDF PDF** (consistent with VNDF sampling). |

DI shading uses **`EvaluateBrdf` in `ShadingHelpers`**, not `RAB_GetSurfaceBrdfSample`, so the VNDF vs non-VNDF difference is **not** in the DI split BRDF evaluation itself, but in other passes sharing `RAB_Surface`.

---

## 6. Visibility and instance masks (cross-cutting)

**Minimal `RAB_VisibilityTest.hlsli` (used by DI shade)**

- `RAB_GetConservativeVisibility`: **binary**, `RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH>`, **`INSTANCE_MASK_OPAQUE`**, `TMax = length(L) - offset` (default offset **0.001**).

**Intermediate `GetFinalVisibility` (used by `ShadeSurfaceWithLightSample` when final visibility enabled)**

- **`float3` throughput** (transparent / alpha-tested path via `considerTransparentMaterial` and non-opaque candidates).
- **`INSTANCE_MASK`** can include **`ALPHA_TESTED`** and **`TRANSPARENT`** when scene flags allow.
- Default visibility ray offset **0.01** and **`TMax = max(offset, length(L) - 2*offset)`**.

So: **Minimal DI always uses cheap opaque conservative visibility**; Intermediate can **skip** final visibility entirely (UI default path depends on `enableFinalVisibility`) or use **richer** visibility with **partial transmission**.

---

## Summary: most plausible causes of **~2–4× lower HDR in MinimalGISample**

1. **Intermediate: `enableFinalVisibility == false`** → **no occlusion** in `ShadeSurfaceWithLightSample` vs **Minimal always shadowing** → large brightness gap in occluded scenes (can easily exceed 2–4× locally).
2. **Checkerboard + `StoreShadingOutput` `×2` compensation** in Intermediate when denoiser is off / specific modes — **~2×** on written lighting if Minimal uses a different reservoir/full-screen convention without compensation.
3. **Compositing:** Minimal **optional tone mapping** on `u_HdrColor` (dims image if enabled); Intermediate compositing **no** tone map in this pass.
4. **`enableTextures` false** in Intermediate would **not** explain Minimal being **lower** (would tend the other way for demodulated buffers); keep **true** when comparing.
5. **Geo-normal test** only in Minimal can **drop** some light samples that Intermediate still shades (usually localized, not a uniform scale).
6. **Roughness == 0:** Intermediate **zero specular** in `EvaluateBrdf` and in reflected radiance helpers; Minimal keeps GGX — **not** a uniform “Minimal darker” explanation, but a **material-dependent** difference.

If you want one experiment to isolate shading vs compositing: **force `enableFinalVisibility` on** in Intermediate, match **tone mapping** state, and compare **raw `u_DiffuseLighting`/`u_SpecularLighting`** before compositing (or with identical recombine math). That separates **visibility / reservoir indexing** from **composite / tone map**.

