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

    if (RAB_IsSurfaceValid(surface))
    {
        RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 1);

        RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(
            g_Const.restirDI.initialSamplingParams.numPrimaryLocalLightSamples,
            0, 0,
            g_Const.restirDI.initialSamplingParams.numPrimaryBrdfSamples,
            g_Const.restirDI.initialSamplingParams.brdfCutoff,
            0.001f);

        RAB_LightSample lightSample = RAB_EmptyLightSample();

        RTXDI_DIReservoir localReservoir = RTXDI_SampleLocalLights(rng, rng, surface,
            sampleParams, ReSTIRDI_LocalLightSamplingMode_UNIFORM, lightBufferParams.localLightBufferRegion, lightSample);

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

        if (RTXDI_IsValidDIReservoir(reservoir) && !selectBrdf)
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
