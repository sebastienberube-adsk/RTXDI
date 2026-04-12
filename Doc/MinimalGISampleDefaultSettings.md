# MinimalGISample Default Settings

This document lists the effective default values for settings in the MinimalGISample application,
mapped against settings visible in the IntermediateSample UI. For each setting, the source of the
value is identified: either explicitly set in MinimalGISample code, or inherited from the SDK defaults
in `Libraries/Rtxdi/Source/ReSTIRDI.cpp`.

---

## Direct Lighting → Spatial Resampling

### Spatial Sampling Radius
- **Default value:** `32.0`
- **Source:** SDK default via `GetDefaultReSTIRDISpatialResamplingParams()` →
  [ReSTIRDI.cpp:78](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L78), `spatialSamplingRadius = 32.0f`
- **MinimalGISample override:** None. The `Render()` function in
  [LightingPasses.cpp:223-226](Samples/MinimalGISample/Source/LightingPasses.cpp#L223-L226)
  only overrides `numSpatialSamples` and `numDisocclusionBoostSamples`; `spatialSamplingRadius`
  is left at the SDK default.

### Discount Naive Samples
- **Default value:** `0` (false)
- **Source:** SDK default via `GetDefaultReSTIRDISpatialResamplingParams()` →
  [ReSTIRDI.cpp:69](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L69), `params = {}` zero-initializes
  all fields including `discountNaiveSamples`.
- **MinimalGISample override:** None at the C++ level. The shader reads it from the constant buffer:
  [DISpatialResampling.hlsl:45](Samples/MinimalGISample/Shaders/DISpatialResampling.hlsl#L45),
  `sparams.discountNaiveSamples = g_Const.restirDI.spatialResamplingParams.discountNaiveSamples;`
- **Note:** In the fused resampling path, it is hardcoded to `false`:
  [DIFusedResampling.hlsl:86](Samples/MinimalGISample/Shaders/DIFusedResampling.hlsl#L86),
  `stparams.discountNaiveSamples = false;`

### Spatial Samples
- **Default value:** `1`
- **Source:** Explicitly set in `LightingPasses::Settings` →
  [LightingPasses.h:52](Samples/MinimalGISample/Source/LightingPasses.h#L52),
  `uint32_t numSpatialSamples = 1;`
- **Applied at:** [LightingPasses.cpp:224](Samples/MinimalGISample/Source/LightingPasses.cpp#L224),
  `spatialParams.numSpatialSamples = localSettings.numSpatialSamples;`

---

## Direct Lighting → Final Visibility / Shading

### Enable Final Visibility
- **Default value:** `true`
- **Source:** SDK default via `GetDefaultReSTIRDIShadingParams()` →
  [ReSTIRDI.cpp:86](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L86), `enableFinalVisibility = true`
- **MinimalGISample override:** None. Shading parameters are read at
  [LightingPasses.cpp:244](Samples/MinimalGISample/Source/LightingPasses.cpp#L244),
  `constants.restirDI.shadingParams = context.GetShadingParameters();`
  without any prior `SetShadingParameters()` call.

### Reuse Final Visibility
- **Default value:** `true`
- **Source:** SDK default via `GetDefaultReSTIRDIShadingParams()` →
  [ReSTIRDI.cpp:90](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L90), `reuseFinalVisibility = true`
- **MinimalGISample override:** None (same flow as above).

### Final Visibility - Max Age
- **Default value:** `4`
- **Source:** SDK default via `GetDefaultReSTIRDIShadingParams()` →
  [ReSTIRDI.cpp:88](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L88), `finalVisibilityMaxAge = 4`
- **MinimalGISample override:** None (same flow as above).

### Final Visibility - Max Distance
- **Default value:** `16.0`
- **Source:** SDK default via `GetDefaultReSTIRDIShadingParams()` →
  [ReSTIRDI.cpp:89](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L89), `finalVisibilityMaxDistance = 16.f`
- **MinimalGISample override:** None (same flow as above).

### Discard Invisible Samples
- **Default value:** `true`
- **Source:** Explicitly set in `LightingPasses::Settings` →
  [LightingPasses.h:47](Samples/MinimalGISample/Source/LightingPasses.h#L47),
  `bool discardInvisibleSamples = true;`
- **Applied at:** [LightingPasses.cpp:220](Samples/MinimalGISample/Source/LightingPasses.cpp#L220),
  `temporalParams.discardInvisibleSamples = localSettings.discardInvisibleSamples;`
- **Note:** The SDK default for this field is `false` (in `GetDefaultReSTIRDITemporalResamplingParams()`),
  but MinimalGISample explicitly overrides it to `true`.

---

## Direct Lighting → Temporal Resampling

### Enable Permutation Sampling
- **Default value:** `true`
- **Source:** SDK default via `GetDefaultReSTIRDITemporalResamplingParams()` →
  [ReSTIRDI.cpp:59](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L59), `enablePermutationSampling = true`
- **MinimalGISample override:** None. The `Render()` function does not set this field on temporalParams.

### Temporal Bias Correction
- **Default value:** `Basic` (when `unbiasedMode = false`, which is the default)
- **Source:** Explicitly set in [LightingPasses.cpp:217-219](Samples/MinimalGISample/Source/LightingPasses.cpp#L217-L219):
  ```cpp
  temporalParams.temporalBiasCorrection = localSettings.unbiasedMode
      ? ReSTIRDI_TemporalBiasCorrectionMode::Raytraced
      : ReSTIRDI_TemporalBiasCorrectionMode::Basic;
  ```
  `unbiasedMode` defaults to `false` in [LightingPasses.h:46](Samples/MinimalGISample/Source/LightingPasses.h#L46).

### Temporal Depth Threshold
- **Default value:** `0.1`
- **Source:** SDK default via `GetDefaultReSTIRDITemporalResamplingParams()` →
  [ReSTIRDI.cpp:63](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L63), `temporalDepthThreshold = 0.1f`
- **MinimalGISample override:** None.

### Temporal Normal Threshold
- **Default value:** `0.5`
- **Source:** SDK default via `GetDefaultReSTIRDITemporalResamplingParams()` →
  [ReSTIRDI.cpp:64](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L64), `temporalNormalThreshold = 0.5f`
- **MinimalGISample override:** None.

### Permutation Sampling Threshold
- **Default value:** `0.9`
- **Source:** SDK default via `GetDefaultReSTIRDITemporalResamplingParams()` →
  [ReSTIRDI.cpp:61](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L61), `permutationSamplingThreshold = 0.9f`
- **MinimalGISample override:** None.

### Max History Length
- **Default value:** `20`
- **Source:** SDK default via `GetDefaultReSTIRDITemporalResamplingParams()` →
  [ReSTIRDI.cpp:60](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L60), `maxHistoryLength = 20`
- **MinimalGISample override:** None.

### Boiling Filter
- **Default value:** Enabled (`true`), strength `0.2`
- **Source:** SDK default via `GetDefaultReSTIRDITemporalResamplingParams()` →
  [ReSTIRDI.cpp:57-58](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L57-L58),
  `enableBoilingFilter = true`, `boilingFilterStrength = 0.2f`
- **MinimalGISample override:** None.
- **Note:** The boiling filter only runs in the Compute Shader (Ray Query) path with `RTXDI_ENABLE_BOILING_FILTER`
  defined. MinimalGISample uses the compute path, so the boiling filter is active.

---

## Direct Lighting → Initial Sampling

### Initial BRDF Samples
- **Default value:** `1`
- **Source:** Explicitly set in `LightingPasses::Settings` →
  [LightingPasses.h:53](Samples/MinimalGISample/Source/LightingPasses.h#L53),
  `uint32_t numInitialBRDFSamples = 1;`
- **Applied at:** [LightingPasses.cpp:212](Samples/MinimalGISample/Source/LightingPasses.cpp#L212),
  `initialSamplingParams.numPrimaryBrdfSamples = localSettings.numInitialBRDFSamples;`

### Initial Infinite Light Samples
- **Default value:** `1`
- **Source:** SDK default via `GetDefaultReSTIRDIInitialSamplingParams()` →
  [ReSTIRDI.cpp:47](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L47), `numPrimaryInfiniteLightSamples = 1`
- **MinimalGISample override:** None. The `Render()` function sets `numPrimaryLocalLightSamples`
  and `numPrimaryBrdfSamples` but leaves `numPrimaryInfiniteLightSamples` at the SDK default.

### Initial Environment Samples
- **Default value:** `1`
- **Source:** SDK default via `GetDefaultReSTIRDIInitialSamplingParams()` →
  [ReSTIRDI.cpp:46](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L46), `numPrimaryEnvironmentSamples = 1`
- **MinimalGISample override:** None.
- **Note:** MinimalGISample does not have an importance-sampled environment map pipeline, so this
  value only takes effect if `environmentMapImportanceSampling` is active and an environment light
  is present. In practice, since MinimalGISample has no environment light or RIS buffer for
  environment map presampling, the environment map sampling code does not generate any samples
  even though this value is 1. The `RTXDI_SampleEnvironmentMap()` function requires
  `RTXDI_ENABLE_PRESAMPLING` to be defined, and MinimalGISample does not use presampling.

### Enable Initial Visibility
- **Default value:** `true`
- **Source:** SDK default via `GetDefaultReSTIRDIInitialSamplingParams()` →
  [ReSTIRDI.cpp:42](Libraries/Rtxdi/Source/ReSTIRDI.cpp#L42), `enableInitialVisibility = true`
- **MinimalGISample override:** None.

### BRDF Sample Cutoff
- **Default value:** `0.0` (disabled)
- **Source:** Explicitly set in `LightingPasses::Settings` →
  [LightingPasses.h:54](Samples/MinimalGISample/Source/LightingPasses.h#L54),
  `float brdfCutoff = 0.f;`
- **Applied at:** [LightingPasses.cpp:213](Samples/MinimalGISample/Source/LightingPasses.cpp#L213),
  `initialSamplingParams.brdfCutoff = localSettings.brdfCutoff;`
- **Note:** The SDK default is `0.0001f`, but MinimalGISample overrides it to `0.0f`,
  effectively disabling BRDF ray shortening.

---

## Scene → Global Emissive Factor

- **No equivalence in MinimalGISample.**
- MinimalGISample does not have a `globalEmissiveFactor` setting. There is no UI control for it
  and the `ResamplingConstants` struct in
  [ShaderParameters.h](Samples/MinimalGISample/Shaders/ShaderParameters.h) has no such field.
- In IntermediateSample, this is a UI-only `static` local variable in `UserInterface::SceneSettings()`
  ([UserInterface.cpp:998](Samples/IntermediateSample/Source/UserInterface.cpp#L998)) that iterates
  over all materials and sets `material->emissiveIntensity`. It is not a shader constant.
- **Recommendation:** Adding this to MinimalGISample is **not recommended** unless matching specific
  IntermediateSample scene configurations is needed. The emissive intensity is a per-material property
  that comes from the scene file. If a uniform scale is needed, it could be added to
  `PrepareLightsPass` or as a constant multiplier in the light preparation shader.

---

## Material Editor → Texture LOD Bias

- **No equivalence in MinimalGISample.**
- MinimalGISample does not have a `textureLodBias` setting or a rasterized G-buffer.
  It uses a compute-shader-based G-buffer pass
  ([GBufferPass.hlsl](Samples/MinimalGISample/Shaders/GBufferPass.hlsl)) that performs ray tracing,
  and the `ResamplingConstants` struct has no `textureLodBias` field.
- In IntermediateSample, the default is `-1.0` as set in
  [GBufferPass.h:42](Samples/IntermediateSample/Source/RenderPasses/GBufferPass.h#L42),
  `float textureLodBias = -1.f;`
- The `lodBias` parameter is used in `SampleMaterialTexturesAtGeometry()` in
  [SceneGeometry.hlsli:156](Samples/MinimalGISample/Shaders/SceneGeometry.hlsli#L156), but in
  MinimalGISample's ray-traced G-buffer, no explicit LOD bias is passed (the caller passes `0`
  or derives it from ray cone / ray differentials depending on implementation).
- **Recommendation:** Adding this is **not recommended** for MinimalGISample. LOD bias is primarily
  useful with rasterized G-buffers where hardware mip selection can be influenced. For ray-traced
  G-buffers, ray differentials or ray cones are the proper mechanism for texture LOD.
