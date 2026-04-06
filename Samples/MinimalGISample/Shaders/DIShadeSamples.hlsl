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

#include <Rtxdi/DI/Reservoir.hlsli>

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(g_Const.restirDI.reservoirBufferParams,
        pixelPosition, g_Const.restirDI.bufferIndices.shadingInputBufferIndex);

    float3 shadingOutput = 0;

    if (RAB_IsSurfaceValid(surface) && RTXDI_IsValidDIReservoir(reservoir))
    {
        RAB_LightInfo lightInfo = RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(reservoir), false);
        RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo,
            surface, RTXDI_GetDIReservoirSampleUV(reservoir));

        shadingOutput = ShadeSurfaceWithLightSample(lightSample, surface)
                      * RTXDI_GetDIReservoirInvPdf(reservoir);

        bool visibility = RAB_GetConservativeVisibility(surface, lightSample);

        if (!visibility)
        {
            shadingOutput = 0;
            RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
            RTXDI_StoreDIReservoir(reservoir, g_Const.restirDI.reservoirBufferParams,
                pixelPosition, g_Const.restirDI.bufferIndices.shadingInputBufferIndex);
        }
    }

    shadingOutput += u_Emissive[pixelPosition].rgb;
    shadingOutput = basicToneMapping(shadingOutput, 0.005);

    u_ShadingOutput[pixelPosition] = float4(shadingOutput, 0);
}
