/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
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

#include <Rtxdi/GI/SpatialResampling.hlsli>

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    uint2 pixelPosition = GlobalIndex;

    if (any(pixelPosition > int2(g_Const.view.viewportSize)))
        return;

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(GlobalIndex, 8);

    const RAB_Surface primarySurface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_GIReservoir reservoir = RTXDI_LoadGIReservoir(g_Const.restirGI.reservoirBufferParams,
        pixelPosition, g_Const.restirGI.bufferIndices.spatialResamplingInputBufferIndex);

    if (RAB_IsSurfaceValid(primarySurface))
    {
        RTXDI_GISpatialResamplingParameters sparams;

        sparams.sourceBufferIndex = g_Const.restirGI.bufferIndices.spatialResamplingInputBufferIndex;
        sparams.biasCorrectionMode = g_Const.restirGI.spatialResamplingParams.spatialBiasCorrectionMode;
        sparams.depthThreshold = g_Const.restirGI.spatialResamplingParams.spatialDepthThreshold;
        sparams.normalThreshold = g_Const.restirGI.spatialResamplingParams.spatialNormalThreshold;
        sparams.numSamples = g_Const.restirGI.spatialResamplingParams.numSpatialSamples;
        sparams.samplingRadius = g_Const.restirGI.spatialResamplingParams.spatialSamplingRadius;

        reservoir = RTXDI_GISpatialResampling(pixelPosition, primarySurface, reservoir,
            rng, g_Const.runtimeParams, g_Const.restirGI.reservoirBufferParams, sparams);
    }

    RTXDI_StoreGIReservoir(reservoir, g_Const.restirGI.reservoirBufferParams,
        pixelPosition, g_Const.restirGI.bufferIndices.spatialResamplingOutputBufferIndex);
}
