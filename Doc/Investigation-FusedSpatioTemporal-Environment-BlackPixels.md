# Investigation: Black Pixels with Fused SpatioTemporal + Procedural Environment

## Problem Summary

When using the FullSample with the LivingRoom scene (indoor, environment not visible):
- **"Fused SpatioTemporal"** DI resampling + **"Procedural"** environment → many fully black pixels
- **"Temporal + Spatial"** DI resampling + **"Procedural"** environment → no issue
- **"Fused SpatioTemporal"** DI resampling + **"None"** environment → no issue (or far fewer)
- Only with ReSTIR DI enabled, ReSTIR GI disabled
- Only visible when viewing raw samples (no AA, no denoising)

---

## Code Paths Analyzed

### "Temporal + Spatial" Path (3 dispatches)

1. **`DIGenerateInitialSamples.hlsl`** → calls `RTXDI_SampleLightsForSurface()` → stores reservoir to buffer A
2. **`DITemporalResampling.hlsl`** → loads buffer A (current initial) + buffer B (prev frame output) → stores to buffer C
3. **`DISpatialResampling.hlsl`** → loads buffer C (temporal output) + spatial neighbors from **buffer C** (current frame) → stores to buffer D
4. **`DIShadeSamples.hlsl`** → loads buffer D → shades

### "Fused SpatioTemporal" Path (1 dispatch)

1. **`DIFusedResampling.hlsl`** → calls `RTXDI_SampleLightsForSurface()` for initial sample, then calls `RTXDI_DISpatioTemporalResampling()` which loads temporal + spatial neighbors from **buffer B (previous frame output)** → stores to buffer A → shades immediately

---

## What Changes When Environment is "Procedural" vs "None"

When the environment is enabled (`lightPresent = 1`), `numPrimaryEnvironmentSamples` stays at its UI value (default `1`). When "None" is selected, the code forces it to `0`:

```cpp
// LightingPasses.cpp:359-361
params.initialSamplingParams.environmentMapImportanceSampling = lightBufferParameters.environmentLightParams.lightPresent;
if (!params.initialSamplingParams.environmentMapImportanceSampling)
    params.initialSamplingParams.numPrimaryEnvironmentSamples = 0;
```

This changes `numMisSamples` in `RTXDI_InitSampleParameters`:
- **"None"**: `numMisSamples = numLocalLight + numBrdf` (e.g. 8+1 = **9**)
- **"Procedural"**: `numMisSamples = numLocalLight + numEnvironment + numBrdf` (e.g. 8+1+1 = **10**)

The `numMisSamples` value is used as the denominator in `RTXDI_FinalizeResampling()` for each sub-reservoir (local, env, BRDF), and drives the MIS weight fractions:

```hlsl
// InitialSampling.hlsli
result.localLightMisWeight = float(numLocalLightSamples) / result.numMisSamples;       // 8/9 → 8/10
result.environmentMapMisWeight = float(numEnvironmentMapSamples) / result.numMisSamples; // 0/9 → 1/10
result.brdfMisWeight = float(numBrdfSamples) / result.numMisSamples;                    // 1/9 → 1/10
```

---

## Root Causes Identified

### 1. **BUG: Missing `canonicalWeight = 1.0f` fallback in fused pairwise MIS** (Critical)

**File:** `Libraries/Rtxdi/Include/Rtxdi/DI/SpatioTemporalResampling.hlsli`, function `RTXDI_DISpatioTemporalResamplingWithPairwiseMIS()`

The fused pairwise MIS path initializes `state.canonicalWeight = 0.0f` but **never** resets it to `1.0f` when no valid neighbors are found (`validSamples == 0`).

Compare with the **spatial-only** pairwise path in `SpatialResampling.hlsli`:
```hlsl
// SpatialResampling.hlsli — line 146-147 (CORRECT)
// If we've seen no usable neighbor samples, set the weight of the central one to 1
state.canonicalWeight = (validSpatialSamples <= 0) ? 1.0f : state.canonicalWeight;
```

The fused path is **MISSING** this line:
```hlsl
// SpatioTemporalResampling.hlsli (MISSING before StreamCanonicalWithPairwiseStep)
// Should add:  state.canonicalWeight = (validSamples <= 0) ? 1.0f : state.canonicalWeight;

RTXDI_StreamCanonicalWithPairwiseStep(state, RAB_GetNextRandom(rng), curSample, surface);
```

**Impact:** When `validSamples == 0` (no spatial or temporal neighbor passes surface tests):
1. `state.canonicalWeight` stays at `0.0f`
2. `RTXDI_StreamCanonicalWithPairwiseStep` computes: `sampleNormalization = canonicalReservoir.weightSum * 0.0 = 0`
3. The canonical sample gets **zero RIS weight** and is **never selected**
4. The reservoir is empty → **black pixel**

This affects all disoccluded or newly-appearing pixels when using the Pairwise bias correction mode. While not exclusively triggered by environment lighting, it makes the system more fragile.

**Note:** This bug only triggers when the user selects "Pairwise" temporal bias correction (enum value 2). The default is "Basic" (enum value 1), which uses the non-pairwise code path in the fused function.

---

### 2. **Structural Difference: Previous-Frame vs Current-Frame Spatial Neighbors** (Primary cause of environment-specific darkening)

This is the fundamental architectural reason why the fused path behaves differently from Temporal+Spatial:

| Aspect | Temporal + Spatial | Fused SpatioTemporal |
|--------|-------------------|---------------------|
| Spatial neighbor source | **Current frame** (temporal output) | **Previous frame** output |
| Spatial neighbors quality | Already temporally repaired | Previous frame's final result |
| Recovery from bad initial samples | Two-phase: temporal replaces bad → spatial uses cleaned data | One-phase: must recover from previous frame neighbors only |

**Why this matters with environment lighting:**

When "Procedural" environment is enabled, some pixels' initial samples select the environment light. For indoor surfaces, these samples are occluded. With `enableInitialVisibility = true` (default), the reservoir is killed:

```hlsl
// FusedResampling.hlsl:77-82 / GenerateInitialSamples.hlsl
if (!RAB_GetConservativeVisibility(surface, lightSample))
{
    RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
    // After this: lightData=0, weightSum=0, but M=1 is preserved
}
```

**In Temporal+Spatial mode:**
1. Initial sampling: ~10% of pixels get killed environment samples (M=1, weightSum=0)
2. Temporal pass: loads the killed sample + previous frame neighbor. The killed sample contributes zero weight via `RTXDI_CombineDIReservoirs` (because `sampleNormalization = weightSum * M = 0 * 1 = 0`). The temporal neighbor's valid local light takes over.
3. Spatial pass: operates on **this frame's temporal output** — all pixels are now repaired with valid local lights. Spatial neighbors propagate good data.
4. **Result: virtually no black pixels.**

**In Fused SpatioTemporal mode:**
1. Initial sampling + fused resampling in one pass
2. Killed environment sample = canonical sample with `weightSum = 0`, `lightData = 0`, `M = 1`
3. Neighbors come from the **previous frame** only
4. In the non-pairwise path: `RTXDI_CombineDIReservoirs(state, curSample, 0.5, curSample.targetPdf)` — killed sample contributes nothing; neighbors from previous frame provide the light.
5. **BUT:** the normalization term `piSum = state.targetPdf * curSample.M` adds `state.targetPdf * 1` to the denominator, which is the selected neighbor light's targetPdf at the current surface. This slightly over-normalizes the result, reducing the final weight compared to the case without environment.
6. More importantly: if previous-frame neighbors ALSO had issues (e.g., they too were affected by environment samples), the fused pass cannot recover because there's no second "spatial cleanup" phase operating on current-frame clean data.

---

### 3. **Invalid Canonical Sample Evaluates as Light Index 0 in Pairwise MIS** (Medium severity)

**File:** `Libraries/Rtxdi/Include/Rtxdi/DI/PairwiseStreaming.hlsli`

When the canonical sample is killed (initial visibility failure):
- `reservoir.lightData = 0` → `RTXDI_GetDIReservoirLightIndex()` returns `0`
- `RTXDI_TargetPdfHelper()` calls `RAB_LoadLightInfo(0, false)` — loads **whatever light happens to be at index 0**
- MIS weights `w1` and `RTXDI_MFactor` are computed using this arbitrary light's targetPdf instead of zero

```hlsl
// PairwiseStreaming.hlsli:44-47 — evaluates canonical, which may be index 0 if killed
float canonicalWeightAtNeighbor = max(0.0f, RTXDI_TargetPdfHelper(canonicalReservor, neighborSurface, false));
float canonicalWeightAtCanonical = max(0.0f, RTXDI_TargetPdfHelper(canonicalReservor, canonicalSurface, false));
```

**Impact analysis:**
- The `w0` weights (for neighbor samples) are computed **correctly** — they only use the neighbor's light
- The `w1` weights use the wrong canonical light targetPdf → `canonicalWeight` accumulates incorrect values
- However, since `canonicalReservoir.weightSum = 0`, the canonical is never selected regardless
- The M-factor for neighbors: `RTXDI_MFactor(canonicalWeightAtNeighbor, canonicalWeightAtCanonical)` uses wrong values, potentially reducing neighbor M contributions
- **Net effect:** slightly wrong effective M values for temporal accumulation, doesn't directly cause black pixels but degrades quality

**Suggested fix:** Guard `RTXDI_StreamNeighborWithPairwiseMIS` calls with a check like:
```hlsl
// If canonical is invalid, its light evaluations are meaningless.
// Set canonical targetPdfs to 0 when canonicalReservor.lightData == 0
```

---

### 4. **MIS Weight Dilution from `numMisSamples` Change** (Contributing factor)

With environment enabled, the initial reservoir's finalized `weightSum` is divided by a larger `numMisSamples`:

```
Local reservoir finalization: weightSum = rawWeightSum / (targetPdf * 10) vs (targetPdf * 9)
```

This means the canonical sample in fused resampling has **~10% less weight** when the environment is enabled. While the MIS math should theoretically compensate for this (the environment samples bring their own weight to the pool), for an indoor scene where environment samples are immediately killed by initial visibility, the local light budget is diluted with no corresponding useful environment contribution.

In `RTXDI_SampleLightsForSurface()`, the sub-reservoirs are combined:
```hlsl
RTXDI_CombineDIReservoirs(state, localReservoir, 0.5, localReservoir.targetPdf);
RTXDI_CombineDIReservoirs(state, environmentReservoir, RAB_GetNextRandom(rng), environmentReservoir.targetPdf);
RTXDI_CombineDIReservoirs(state, brdfReservoir, RAB_GetNextRandom(rng), brdfReservoir.targetPdf);
RTXDI_FinalizeResampling(state, 1.0, 1.0);
state.M = 1;
```

When the environment reservoir wins the combination but has a killed sample (due to initial visibility), its weight gets zeroed out. But the `numMisSamples` dilution has already affected the local sub-reservoir's weight. The net effect: **some pixels that would have had a strong local light initial sample now have a dead environment sample**, with no benefit.

---

### 5. **The `targetPdf` Includes `1/solidAnglePdf` — Large Variation for Environment Lights** (Contributing factor)

```hlsl
// RAB_LightSampling.hlsli:88-92
float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface)
{
    if (lightSample.solidAnglePdf <= 0) return 0;
    return RAB_GetReflectedLuminanceForSurface(...) / lightSample.solidAnglePdf;
}
```

For environment lights, `solidAnglePdf` comes from the importance-sampled environment map PDF. This can be very small for dim regions, making `targetPdf` very large even when actual reflected luminance is moderate. For indoor surfaces, the N·L term can be zero (surface faces away from environment direction) or non-zero depending on exact geometry.

This causes high targetPdf variance across surfaces in pairwise MIS, leading to aggressive M-factor reduction via `RTXDI_MFactor`:
```hlsl
float RTXDI_MFactor(float q0, float q1)
{
    return (q0 <= 0.0f) ? 1.0f : clamp(pow(min(q1 / q0, 1.0f), 8.0f), 0.0f, 1.0f);
}
```

If a neighbor has an environment sample with high targetPdf at its own surface but zero at the canonical surface, `MFactor(neighborPdfAtNeighbor, 0) = pow(0, 8) = 0`, meaning the neighbor's effective M contribution is completely suppressed.

---

## Summary of Differences: Fused SpatioTemporal vs Temporal + Spatial

| Feature | Fused | Temporal + Spatial |
|---------|-------|--------------------|
| Passes | 1 (initial + resample + shade) | 4 (initial + temporal + spatial + shade) |
| Spatial neighbor source | Previous frame | Current frame temporal output |
| Recovery from bad initial | One-phase from prev frame neighbors | Two-phase: temporal repair then spatial cleanup |
| Pairwise canonicalWeight fallback | **MISSING** ⚠️ (when zero neighbors) | N/A (different flow) |
| Sensitivity to environment samples | **High**: bad initials propagate via prev-frame feedback | **Low**: temporal pass immediately replaces bad initials |

---

## Recommended Investigation Steps

1. **Verify the pairwise canonicalWeight bug:** Add the missing fallback line and test if it reduces black pixels in the pairwise bias correction mode.

2. **Test with initial visibility disabled:** If `enableInitialVisibility = false`, environment samples survive with non-zero weight but produce zero radiance in final shading. This would confirm whether the mechanism is killed reservoirs vs zero-visibility shading.

3. **Count environment sample frequency:** Log how many pixels pick environment samples during initial sampling. This quantifies the "bad initial sample" rate.

4. **Guard pairwise MIS against invalid canonical:** Add a validity check before evaluating canonical targetPdf in `RTXDI_StreamNeighborWithPairwiseMIS`.

---

## Proposed Fixes

### Fix 1: Pairwise canonicalWeight fallback (in `SpatioTemporalResampling.hlsli`)
```hlsl
// Before RTXDI_StreamCanonicalWithPairwiseStep, add:
state.canonicalWeight = (validSamples <= 0) ? 1.0f : state.canonicalWeight;

RTXDI_StreamCanonicalWithPairwiseStep(state, RAB_GetNextRandom(rng),
    curSample, surface);
```

### Fix 2: Guard pairwise MIS against invalid canonical (in `PairwiseStreaming.hlsli`)
```hlsl
bool RTXDI_StreamNeighborWithPairwiseMIS(...)
{
    // If canonical is invalid, treat its targetPdf contributions as zero
    bool canonicalValid = (canonicalReservor.lightData != 0);
    float canonicalWeightAtNeighbor = canonicalValid
        ? max(0.0f, RTXDI_TargetPdfHelper(canonicalReservor, neighborSurface, false))
        : 0.0f;
    float canonicalWeightAtCanonical = canonicalValid
        ? max(0.0f, RTXDI_TargetPdfHelper(canonicalReservor, canonicalSurface, false))
        : 0.0f;
    // ... rest unchanged
}
```

### Fix 3 (Optional): Skip environment samples when not useful
For purely indoor scenes, applications could set `numPrimaryEnvironmentSamples = 0` even when the environment is present, to avoid wasting sampling budget. This is a user-side mitigation.
