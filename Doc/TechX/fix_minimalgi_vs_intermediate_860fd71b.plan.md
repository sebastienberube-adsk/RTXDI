---
name: Fix MinimalGI vs Intermediate
overview: Systematically identify and fix all key rendering differences between MinimalGISample and IntermediateSample so that `SampleImageTests.MinimalGI_vs_Intermediate_NoDI` passes, by adding missing mechanisms to MinimalGI or disabling extras in IntermediateSample, one key element per commit.
todos:
  - id: accumulation
    content: Port AccumulationPass from IntermediateSample to MinimalGI (copy shader+class, add AccumulatedColor texture, add --aa-mode CLI, wire into render loop)
    status: completed
  - id: tonemapping
    content: Fix/verify ToneMappingPass in MinimalGI so textures are visible (check exposure params, verify HdrColor->LdrColor pipeline, match IntermediateSample's TM params)
    status: completed
  - id: di-shading
    content: "Align DI shading: add final visibility reuse to MinimalGI, remove geoNormal check, add roughness==0 specular zeroing"
    status: completed
  - id: rtxdi-params
    content: "Align RTXDI params: set brdfCutoff=0.0001, numPrimaryInfiniteLightSamples=2 in MinimalGI"
    status: completed
  - id: test-config
    content: "Update SampleTests.cpp: both use --aa-mode ACC, same TM, same frame count, add --rasterize-gbuffer 0 to IntermediateSample"
    status: completed
  - id: save-path
    content: Ensure MinimalGI saves from LdrColor (not backBuffer), matching IntermediateSample
    status: completed
isProject: false
---

# Fix MinimalGI_vs_Intermediate_NoDI by aligning rendering pipelines

## Summary of All Differences Found

The two samples share the same core RTXDI shading logic, but differ in several "key elements" that affect the final image. Each must be addressed independently. Below is the full list, organized by priority.

---

## Key Element 1: Tonemapping (fix "missing textures")

**Problem:** MinimalGI currently has a `--tone-mapping 2` mode that uses the donut `ToneMappingPass` with adaptive exposure. The user reports "most textures are missing" in the output BMP -- likely caused by the adaptive exposure being mis-adapted (scene too dark/bright, crushing all texture detail).

**Root cause:** The `ToneMappingPass` in MinimalGI was added hastily. The exposure params (`exposureBias=-1.0`, `minAdaptedLuminance=0.002`, `maxAdaptedLuminance=0.2`) may be correct, but the histogram pass reads from `HdrColor` which has raw linear HDR. If the overall scene luminance is very low (as we observed: tile averages ~0.03-0.12), the adapted exposure could swing wildly.

**Fix approach:** Rather than adaptive exposure (which adapts independently per sample), both samples should use the **same fixed tonemapping**. Two options:

- **Option A (preferred):** Keep using the donut `ToneMappingPass` in MinimalGI but also pass it through the same `Resolve` pipeline step (accumulation first, then tonemap). This requires adding accumulation first (Key Element 2).
- **Option B (fallback):** Disable tonemapping in both samples for the test (`--tone-mapping 0` in both) and accept darker images. Only viable if the underlying HDR values match.

**Files:**

- [Samples/MinimalGISample/Source/main.cpp](Samples/MinimalGISample/Source/main.cpp): ToneMappingPass creation and render path (~lines 357-427)
- [Samples/MinimalGISample/Source/LightingPasses.cpp](Samples/MinimalGISample/Source/LightingPasses.cpp): `enableBasicToneMapping` constant (~line 232)

---

## Key Element 2: Frame Accumulation (AA)

**Problem:** The test currently runs IntermediateSample with `--aa-mode ACC` (128-frame accumulation) but MinimalGI has no accumulation at all. This produces a smooth converged image on one side and a noisy single-frame image on the other -- apples to oranges.

**Why accumulation matters here:** Without accumulation, the IntermediateSample produces extremely dark single-frame output (~0.007 avg). With accumulation over 128 frames, it brightens to ~0.12. This is because individual frames have high variance (ReSTIR DI stochastic sampling), and accumulation averages the variance away.

**Fix approach:** Port the `AccumulationPass` from IntermediateSample to MinimalGI. This involves:

1. Copy `AccumulationPass.h/cpp` and `AccumulationPass.hlsl` from IntermediateSample to MinimalGI (adapting include paths to MinimalGI's `RenderTargets`)
2. Add `AccumulatedColor` texture to MinimalGI's `RenderTargets`
3. Add `--aa-mode` CLI option to MinimalGI
4. Wire up the accumulation in `main.cpp`: after compositing to HdrColor, run AccumulationPass, then tonemap the accumulated result, save from LdrColor

**Files to port:**

- [Samples/IntermediateSample/Source/RenderPasses/AccumulationPass.h](Samples/IntermediateSample/Source/RenderPasses/AccumulationPass.h)
- [Samples/IntermediateSample/Source/RenderPasses/AccumulationPass.cpp](Samples/IntermediateSample/Source/RenderPasses/AccumulationPass.cpp)
- [Samples/IntermediateSample/Shaders/AccumulationPass.hlsl](Samples/IntermediateSample/Shaders/AccumulationPass.hlsl)

**Files to modify:**

- [Samples/MinimalGISample/Source/RenderTargets.h](Samples/MinimalGISample/Source/RenderTargets.h) / `.cpp`: add `AccumulatedColor` texture (RGBA16_FLOAT, UAV)
- [Samples/MinimalGISample/Source/Testing.h](Samples/MinimalGISample/Source/Testing.h) / `.cpp`: add `--aa-mode` option
- [Samples/MinimalGISample/Source/main.cpp](Samples/MinimalGISample/Source/main.cpp): create AccumulationPass, wire into render loop

---

## Key Element 3: Save Path Alignment

**Problem:** IntermediateSample saves from `m_renderTargets->LdrColor` (SRGBA8_UNORM). MinimalGI saves from the swap chain back buffer (or LdrColor when tone-mapping 2 is active). This can cause subtle format/gamma differences.

**Fix:** MinimalGI should always save from `LdrColor` after tonemapping, exactly like IntermediateSample does (line 1300 of IntermediateSample's main.cpp).

**Files:**

- [Samples/MinimalGISample/Source/main.cpp](Samples/MinimalGISample/Source/main.cpp) ~line 437

---

## Key Element 4: DI Shading Differences

Three sub-differences in the DI shade pass:

### 4a. Visibility handling

- **MinimalGI** ([DIShadeSamples.hlsl](Samples/MinimalGISample/Shaders/DIShadeSamples.hlsl)): Uses `RAB_GetConservativeVisibility` (binary, always traces a fresh shadow ray)
- **IntermediateSample** ([ShadingHelpers.hlsli](Samples/IntermediateSample/Shaders/LightingPasses/ShadingHelpers.hlsli)): Uses `enableFinalVisibility` + `reuseFinalVisibility` via `RTXDI_GetDIReservoirVisibility`

**Fix:** Add final visibility + reuse support to MinimalGI's DI shade pass, matching IntermediateSample's `ShadeSurfaceWithLightSample` logic. This is the most impactful shading change.

### 4b. Geometric normal check

- **MinimalGI**: Requires `dot(L, surface.geoNormal) > 0` before BRDF evaluation
- **IntermediateSample**: No such check

**Fix:** Remove the geoNormal check from MinimalGI (or add it to IntermediateSample). Removing from MinimalGI is simpler and aligns with IntermediateSample.

### 4c. Roughness==0 specular handling

- **MinimalGI** ([ShadingHelpers.hlsli](Samples/MinimalGISample/Shaders/ShadingHelpers.hlsli)): Always evaluates GGX specular
- **IntermediateSample** ([ShadingHelpers.hlsli](Samples/IntermediateSample/Shaders/LightingPasses/ShadingHelpers.hlsli)): Forces `specular = 0` when `roughness == 0`

**Fix:** Add the `roughness == 0` check to MinimalGI's `EvaluateBrdf`.

---

## Key Element 5: RTXDI Parameter Alignment

Parameter differences between MinimalGI and IntermediateSample (Medium preset):


| Parameter                        | MinimalGI (current)                 | IntermediateSample Medium | Action                                       |
| -------------------------------- | ----------------------------------- | ------------------------- | -------------------------------------------- |
| `brdfCutoff`                     | 0.0                                 | 0.0001 (library default)  | Set MinimalGI to 0.0001                      |
| `numPrimaryInfiniteLightSamples` | 1 (library default, not overridden) | 2                         | Set MinimalGI to 2                           |
| `enableBoilingFilter`            | true (default)                      | true                      | OK                                           |
| `boilingFilterStrength`          | 0.2 (default)                       | 0.2                       | OK                                           |
| `temporalBiasCorrection`         | Raytraced (forced in current code)  | Raytraced                 | OK                                           |
| `discardInvisibleSamples`        | true (forced in current code)       | true                      | OK                                           |
| `spatialBiasCorrection`          | Basic (when not unbiased)           | Basic                     | OK                                           |
| `numDisocclusionBoostSamples`    | 8 (default)                         | 8                         | OK                                           |
| `reuseFinalVisibility`           | true (default)                      | true                      | OK (but MinimalGI doesn't USE it in shading) |
| `enableFinalVisibility`          | true (default)                      | true                      | OK (but MinimalGI doesn't USE it in shading) |


**Fix:** Set `brdfCutoff = 0.0001f` and `numPrimaryInfiniteLightSamples = 2` in MinimalGI's `LightingPasses.cpp`.

**Files:**

- [Samples/MinimalGISample/Source/LightingPasses.cpp](Samples/MinimalGISample/Source/LightingPasses.cpp) ~line 184-206

---

## Key Element 6: G-Buffer Method

- **MinimalGI**: Always uses **ray-traced** G-buffer (compute shader tracing primary rays)
- **IntermediateSample**: Uses **rasterized** G-buffer by default (`rasterizeGBuffer = true`)

**Fix:** Pass `--rasterize-gbuffer 0` to IntermediateSample in the test, so both use ray-traced G-buffers. This is a simple parameter change in the test.

---

## Key Element 7: Light Preparation

- **MinimalGI** ([PrepareLightsPass.cpp](Samples/MinimalGISample/Source/PrepareLightsPass.cpp)): Only emissive mesh geometry as local lights. No infinite lights, no environment lights.
- **IntermediateSample** ([PrepareLightsPass.cpp](Samples/IntermediateSample/Source/RenderPasses/PrepareLightsPass.cpp)): Emissive meshes + primitive lights (directional/spot/point) + environment light

For this scene (`livingroom_Original.scene.json`), environment map is disabled by default (`environmentMapIndex = -1`). So this difference may be minor if the scene only has emissive mesh lights. However, the IntermediateSample's `PrepareLightsPass` also generates a **LocalLightPdfTexture** for importance sampling, which MinimalGI's doesn't. This affects reservoir selection quality.

**Fix:** Not critical for passing the test, but note that MinimalGI uses uniform light PDF while IntermediateSample uses importance-based PDF. This difference is mitigated by temporal resampling convergence.

---

## Test Configuration

After all key elements are addressed, the test command should be:

```
MinimalGISample:    --disable-gi --aa-mode ACC --tone-mapping 2 --save-frame 128
IntermediateSample: --indirect-mode NONE --aa-mode ACC --rasterize-gbuffer 0 --save-frame 128
```

Both use: accumulation, same tonemapping approach (donut ToneMappingPass), same frame count, ray-traced G-buffer.

---

## Iteration Order

Address in this order (each as a separate commit):

1. **Accumulation** - Add AccumulationPass to MinimalGI (biggest impact, enables fair comparison)
2. **Tonemapping** - Fix/verify ToneMappingPass in MinimalGI (fixes "missing textures")
3. **DI Shading** - Add final visibility reuse, remove geoNormal check, add roughness==0 check
4. **RTXDI Params** - Align brdfCutoff, infiniteLightSamples
5. **Test Config** - Set matching parameters in SampleTests.cpp, including `--rasterize-gbuffer 0`
6. **Save Path** - Ensure both save from LdrColor

After each commit: build, run test, check delta, diagnose remaining failures.