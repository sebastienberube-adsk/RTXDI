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

#include <Rtxdi/DI/Reservoir.hlsli>

#include "ShadingHelpers.hlsli"

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(g_Const.restirDI.reservoirBufferParams,
        pixelPosition, g_Const.restirDI.bufferIndices.shadingInputBufferIndex);

    float3 diffuse = 0;
    float3 specular = 0;

    if (RAB_IsSurfaceValid(surface) && RTXDI_IsValidDIReservoir(reservoir))
    {
        RAB_LightInfo lightInfo = RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(reservoir), false);
        RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo,
            surface, RTXDI_GetDIReservoirSampleUV(reservoir));

        if (lightSample.solidAnglePdf > 0)
        {
            float3 L = normalize(lightSample.position - surface.worldPos);

            if (dot(L, surface.geoNormal) > 0)
            {
                float weight = RTXDI_GetDIReservoirInvPdf(reservoir) / lightSample.solidAnglePdf;
                SplitBrdf brdf = EvaluateBrdf(surface, lightSample.position);

                diffuse = brdf.demodulatedDiffuse * lightSample.radiance * weight;
                specular = brdf.specular * lightSample.radiance * weight;
                specular = DemodulateSpecular(surface.material.specularF0, specular);
            }

            bool visibility = RAB_GetConservativeVisibility(surface, lightSample);
            if (!visibility)
            {
                diffuse = 0;
                specular = 0;
                RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
                RTXDI_StoreDIReservoir(reservoir, g_Const.restirDI.reservoirBufferParams,
                    pixelPosition, g_Const.restirDI.bufferIndices.shadingInputBufferIndex);
            }
        }
    }

    StoreShadingOutput(pixelPosition, diffuse, specular, true);
}
