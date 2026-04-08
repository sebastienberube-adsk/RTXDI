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
#define ENABLE_METAL_ROUGH_RECONSTRUCTION 1

#include "RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"
#include "PrimaryRays.hlsli"

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    PrimarySurfaceOutput primary = TracePrimaryRay(pixelPosition);

    u_GBufferDepth[pixelPosition] = primary.surface.viewDepth;
    u_GBufferNormals[pixelPosition] = ndirToOctUnorm32(primary.surface.normal);
    u_GBufferGeoNormals[pixelPosition] = ndirToOctUnorm32(primary.surface.geoNormal);
    u_GBufferDiffuseAlbedo[pixelPosition] = Pack_R11G11B10_UFLOAT(primary.surface.material.diffuseAlbedo);
    u_GBufferSpecularRough[pixelPosition] = Pack_R8G8B8A8_Gamma_UFLOAT(float4(primary.surface.material.specularF0, primary.surface.material.roughness));
    u_MotionVectors[pixelPosition] = float4(primary.motionVector, 0);
    u_Emissive[pixelPosition] = float4(primary.emissiveColor, 0);
}
