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

#include <Rtxdi/DI/SpatioTemporalResampling.hlsli>

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    float3 motionVector = u_MotionVectors[pixelPosition].xyz;
    float3 emissiveColor = u_Emissive[pixelPosition].rgb;

    RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(g_Const.restirDI.reservoirBufferParams,
        pixelPosition, g_Const.restirDI.bufferIndices.initialSamplingOutputBufferIndex);

    if (RAB_IsSurfaceValid(surface))
    {
        RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 2);

        RAB_LightSample lightSample = RAB_EmptyLightSample();
        if (RTXDI_IsValidDIReservoir(reservoir))
        {
            RAB_LightInfo lightInfo = RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(reservoir), false);
            lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, RTXDI_GetDIReservoirSampleUV(reservoir));
        }

        if (g_Const.enableResampling)
        {
            RTXDI_DISpatioTemporalResamplingParameters stparams;
            stparams.screenSpaceMotion = motionVector;
            stparams.sourceBufferIndex = g_Const.restirDI.bufferIndices.temporalResamplingInputBufferIndex;
            stparams.maxHistoryLength = g_Const.restirDI.temporalResamplingParams.maxHistoryLength;
            stparams.biasCorrectionMode = g_Const.restirDI.temporalResamplingParams.temporalBiasCorrection;
            stparams.depthThreshold = g_Const.restirDI.temporalResamplingParams.temporalDepthThreshold;
            stparams.normalThreshold = g_Const.restirDI.temporalResamplingParams.temporalNormalThreshold;
            stparams.numSamples = g_Const.restirDI.spatialResamplingParams.numSpatialSamples + 1;
            stparams.numDisocclusionBoostSamples = g_Const.restirDI.spatialResamplingParams.numDisocclusionBoostSamples;
            stparams.samplingRadius = g_Const.restirDI.spatialResamplingParams.spatialSamplingRadius;
            stparams.enableVisibilityShortcut = g_Const.restirDI.temporalResamplingParams.discardInvisibleSamples;
            stparams.enablePermutationSampling = true;
            stparams.uniformRandomNumber = g_Const.frameIndex;
            stparams.discountNaiveSamples = false;

            int2 temporalSamplePixelPos = -1;

            reservoir = RTXDI_DISpatioTemporalResampling(pixelPosition, surface, reservoir,
                    rng, g_Const.runtimeParams, g_Const.restirDI.reservoirBufferParams, stparams, temporalSamplePixelPos, lightSample);
        }

        float3 shadingOutput = 0;

        if (RTXDI_IsValidDIReservoir(reservoir))
        {
            shadingOutput = ShadeSurfaceWithLightSample(lightSample, surface)
                          * RTXDI_GetDIReservoirInvPdf(reservoir);

            bool visibility = RAB_GetConservativeVisibility(surface, lightSample);
            if (!visibility)
            {
                shadingOutput = 0;
                RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
            }
        }

        shadingOutput += emissiveColor;
        shadingOutput = basicToneMapping(shadingOutput, 0.005);

        u_ShadingOutput[pixelPosition] = float4(shadingOutput, 0);
    }
    else
    {
        u_ShadingOutput[pixelPosition] = 0;
    }

    RTXDI_StoreDIReservoir(reservoir, g_Const.restirDI.reservoirBufferParams, pixelPosition, g_Const.restirDI.bufferIndices.shadingInputBufferIndex);
}
