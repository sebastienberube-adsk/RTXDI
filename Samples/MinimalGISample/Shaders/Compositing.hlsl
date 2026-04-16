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

bool outputDiagMode(uint2 px, int diagMode)
{
    if (diagMode == 1)
    {
        float roughness = Unpack_R8G8B8A8_Gamma_UFLOAT(u_GBufferSpecularRough[px]).a;
        u_HdrColor[px] = float4(roughness * float3(1, 1, 1), 1);
        return true;
    }
    else if (diagMode == 2)
    {
        float3 normal = 0.5 + 0.5 * octToNdirUnorm32(u_GBufferNormals[px]);
        u_HdrColor[px] = float4(normal, 1);
        return true;
    }
    else if (diagMode == 3)
    {
        float3 diffuseAlbedo = Unpack_R11G11B10_UFLOAT(u_GBufferDiffuseAlbedo[px]);
        u_HdrColor[px] = float4(diffuseAlbedo, 1);
        return true;
    }
    else if (diagMode == 4)
    {
        float3 specularF0 = Unpack_R8G8B8A8_Gamma_UFLOAT(u_GBufferSpecularRough[px]).rgb;
        u_HdrColor[px] = float4(specularF0, 1);
        return true;
    }
    else if (diagMode == 5)
    {
        float depth = u_GBufferDepth[px];
        float normalizedDepth = saturate(depth / 10.0);
        u_HdrColor[px] = float4(normalizedDepth, normalizedDepth, normalizedDepth, 1);
        return true;
    }
    return false;
}

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    if (g_Const.diagMode > 0 && outputDiagMode(pixelPosition, g_Const.diagMode))
        return;

    float3 diffuseAlbedo = Unpack_R11G11B10_UFLOAT(u_GBufferDiffuseAlbedo[pixelPosition]);
    float3 specularF0 = Unpack_R8G8B8A8_Gamma_UFLOAT(u_GBufferSpecularRough[pixelPosition]).rgb;
    float3 emissive = u_Emissive[pixelPosition].rgb;

    float3 diffuse = u_DiffuseLighting[pixelPosition].rgb;
    float3 specular = u_SpecularLighting[pixelPosition].rgb;

    float depth = u_GBufferDepth[pixelPosition];
    float3 color = 0;

    if (depth != BACKGROUND_DEPTH)
    {
        color = diffuse * diffuseAlbedo + specular * max(0.01, specularF0) + emissive;
    }
    else if (g_Const.sceneConstants.enableEnvironmentMap)
    {
        RayDesc primaryRay = setupPrimaryRay(pixelPosition, g_Const.view);
        color = GetEnvironmentRadiance(primaryRay.Direction);
    }

    if (any(isnan(color)))
        color = float3(0, 0, 1);

    if (g_Const.enableBasicToneMapping)
        color = basicToneMapping(color, g_Const.basicTonemapBias);

    u_HdrColor[pixelPosition] = float4(color, 1.0);
}
