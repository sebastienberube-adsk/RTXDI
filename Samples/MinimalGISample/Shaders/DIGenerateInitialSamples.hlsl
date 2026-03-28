/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#define RTXDI_ENABLE_PRESAMPLING 0

#include "RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include <Rtxdi/DI/InitialSampling.hlsli>

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    const RTXDI_LightBufferParameters lightBufferParams = g_Const.lightBufferParams;

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();

    // Unlike the full sample, we check if the surface is valid before generating any samples.
    // Not sure why, maybe this is because the minimal sample only generates local light samples,
    // which require a valid surface. The full sample generates local, infinite, environment,
    // and BRDF samples, and some of those sample types can be generated even for invalid surfaces
    // (e.g. infinite lights, environment map).
    if (RAB_IsSurfaceValid(surface))
    {
        // Init random sampler state. tileRng is not actually needed, because presampling (RIS buffers / ReGIR) is disabled.
	    // It's seeded per-tile (pixelPosition / RTXDI_TILE_SIZE_IN_PIXELS) so that all pixels within the
	    // same screen tile share the same random sequence when indexing into a pre-sampled light list.
	    // This ensures coherent tile-level light selection.
        RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 1);
        RAB_RandomSamplerState tileRng = rng;

        RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(
            g_Const.restirDI.initialSamplingParams.numPrimaryLocalLightSamples,
            0, // No infinite/directional light samples in the minimal sample
            0, // No environment map samples in the minimal sample
            g_Const.restirDI.initialSamplingParams.numPrimaryBrdfSamples,
            g_Const.restirDI.initialSamplingParams.brdfCutoff,
            0.001f);

        RAB_LightSample lightSample = RAB_EmptyLightSample();

        // For simplicity, the minimal sample only generates local light samples. The full sample generates local, infinite, environment, and BRDF samples.
        // RTXDI_SampleLightsForSurface is a convenience function that internally calls:
        //  - RTXDI_SampleLocalLights — local lights
        //  - RTXDI_SampleInfiniteLights — infinite/directional lights
        //  - RTXDI_SampleEnvironmentMap — environment map (when presampling is enabled)
        //  - RTXDI_SampleBrdf — BRDF-guided samples
        RTXDI_DIReservoir localReservoir = RTXDI_SampleLocalLights(rng, tileRng, surface,
            sampleParams, ReSTIRDI_LocalLightSamplingMode_UNIFORM,
            lightBufferParams.localLightBufferRegion, lightSample);

        RTXDI_CombineDIReservoirs(reservoir, localReservoir, 0.5, localReservoir.targetPdf);

        RAB_LightSample brdfSample = RAB_EmptyLightSample();
        RTXDI_DIReservoir brdfReservoir = RTXDI_SampleBrdf(rng, surface, sampleParams, lightBufferParams, brdfSample);

        bool selectBrdf = RTXDI_CombineDIReservoirs(reservoir, brdfReservoir, RAB_GetNextRandom(rng), brdfReservoir.targetPdf);
        if (selectBrdf)
        {
            lightSample = brdfSample;
        }

        RTXDI_FinalizeResampling(reservoir, 1.0, 1.0);
        reservoir.M = 1;

        if (g_Const.restirDI.initialSamplingParams.enableInitialVisibility
            && RTXDI_IsValidDIReservoir(reservoir) && !selectBrdf)
        {
            if (!RAB_GetConservativeVisibility(surface, lightSample))
            {
                RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
            }
        }
    }

    RTXDI_StoreDIReservoir(reservoir, g_Const.restirDI.reservoirBufferParams,
        pixelPosition, g_Const.restirDI.bufferIndices.initialSamplingOutputBufferIndex);
}
