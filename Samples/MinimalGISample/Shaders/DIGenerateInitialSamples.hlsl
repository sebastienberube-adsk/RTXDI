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

#include "RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include <Rtxdi/DI/InitialSampling.hlsli>

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    const RTXDI_LightBufferParameters lightBufferParams = g_Const.lightBufferParams;

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();

    if (RAB_IsSurfaceValid(surface))
    {
        RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 1);
        RAB_RandomSamplerState tileRng = RAB_InitRandomSampler(pixelPosition / RTXDI_TILE_SIZE_IN_PIXELS, 1);

        RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(
            g_Const.restirDI.initialSamplingParams.numPrimaryLocalLightSamples,
            g_Const.restirDI.initialSamplingParams.numPrimaryInfiniteLightSamples,
            g_Const.restirDI.initialSamplingParams.numPrimaryEnvironmentSamples,
            g_Const.restirDI.initialSamplingParams.numPrimaryBrdfSamples,
            g_Const.restirDI.initialSamplingParams.brdfCutoff,
            0.001f);

        RAB_LightSample lightSample = RAB_EmptyLightSample();

        reservoir = RTXDI_SampleLightsForSurface(rng, tileRng, surface,
            sampleParams, lightBufferParams, ReSTIRDI_LocalLightSamplingMode_UNIFORM,
#ifdef RTXDI_ENABLE_PRESAMPLING
            g_Const.localLightsRISBufferSegmentParams, g_Const.environmentLightRISBufferSegmentParams,
#endif
            lightSample);

        if (g_Const.restirDI.initialSamplingParams.enableInitialVisibility && RTXDI_IsValidDIReservoir(reservoir))
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
