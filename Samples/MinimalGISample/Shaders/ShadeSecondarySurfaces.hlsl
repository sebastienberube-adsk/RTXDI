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

#include <Rtxdi/DI/InitialSampling.hlsli>
#include <Rtxdi/GI/Reservoir.hlsli>

#include "ShadingHelpers.hlsli"

static const float c_MaxIndirectRadiance = 10;

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    if (any(pixelPosition > int2(g_Const.view.viewportSize)))
        return;

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 6);
    RAB_RandomSamplerState tileRng = RAB_InitRandomSampler(pixelPosition / RTXDI_TILE_SIZE_IN_PIXELS, 1);

    const uint gbufferIndex = RTXDI_ReservoirPositionToPointer(g_Const.restirDI.reservoirBufferParams, pixelPosition, 0);

    RAB_Surface primarySurface = RAB_GetGBufferSurface(pixelPosition, false);

    SecondaryGBufferData secondaryGBufferData = u_SecondaryGBuffer[gbufferIndex];

    const float3 throughput = Unpack_R16G16B16A16_FLOAT(secondaryGBufferData.throughputAndFlags).rgb;
    const uint secondaryFlags = secondaryGBufferData.throughputAndFlags.y >> 16;
    const bool isValidSecondarySurface = any(throughput != 0);
    const bool isSpecularRay = (secondaryFlags & kSecondaryGBuffer_IsSpecularRay) != 0;
    const bool isDeltaSurface = (secondaryFlags & kSecondaryGBuffer_IsDeltaSurface) != 0;
    const bool isEnvironmentMap = (secondaryFlags & kSecondaryGBuffer_IsEnvironmentMap) != 0;

    RAB_Surface secondarySurface;
    float3 radiance = secondaryGBufferData.emission;

    secondarySurface.worldPos = secondaryGBufferData.worldPos;
    secondarySurface.viewDepth = 1.0;
    secondarySurface.normal = octToNdirUnorm32(secondaryGBufferData.normal);
    secondarySurface.geoNormal = secondarySurface.normal;
    secondarySurface.material.diffuseAlbedo = Unpack_R11G11B10_UFLOAT(secondaryGBufferData.diffuseAlbedo);
    float4 specularRough = Unpack_R8G8B8A8_Gamma_UFLOAT(secondaryGBufferData.specularAndRoughness);
    secondarySurface.material.specularF0 = specularRough.rgb;
    secondarySurface.material.roughness = specularRough.a;
    secondarySurface.diffuseProbability = getSurfaceDiffuseProbability(secondarySurface);
    secondarySurface.viewDir = normalize(primarySurface.worldPos - secondarySurface.worldPos);

    if (isValidSecondarySurface && !isEnvironmentMap)
    {
        RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(
            g_Const.restirDI.initialSamplingParams.numPrimaryLocalLightSamples,
            0, 0,
            0,      // numBrdfSamples
            0.f,    // brdfCutoff
            0.001f);

        RAB_LightSample lightSample;
        RTXDI_DIReservoir reservoir = RTXDI_SampleLightsForSurface(rng, tileRng, secondarySurface,
            sampleParams, g_Const.lightBufferParams, ReSTIRDI_LocalLightSamplingMode_UNIFORM,
            lightSample);

        if (RTXDI_IsValidDIReservoir(reservoir))
        {
            float3 indirectDiffuse = 0;
            float3 indirectSpecular = 0;
            ShadeSurfaceWithLightSample(reservoir, secondarySurface, lightSample,
                false, indirectDiffuse, indirectSpecular);

            radiance += indirectDiffuse * secondarySurface.material.diffuseAlbedo + indirectSpecular;
        }

        float indirectLuminance = calcLuminance(radiance);
        if (indirectLuminance > c_MaxIndirectRadiance)
            radiance *= c_MaxIndirectRadiance / indirectLuminance;
    }

    bool enableReSTIRGI = g_Const.brdfPT.enableReSTIRGI;
    bool outputShadingResult = true;

    if (enableReSTIRGI)
    {
        RTXDI_GIReservoir reservoir = RTXDI_EmptyGIReservoir();

        outputShadingResult = isSpecularRay && isDeltaSurface;

        if (isValidSecondarySurface && !outputShadingResult)
        {
            reservoir = RTXDI_MakeGIReservoir(secondarySurface.worldPos,
                secondarySurface.normal, radiance, secondaryGBufferData.pdf);
        }

        RTXDI_StoreGIReservoir(reservoir, g_Const.restirGI.reservoirBufferParams,
            pixelPosition, g_Const.restirGI.bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex);

        secondaryGBufferData.emission = outputShadingResult ? 0 : radiance;
        u_SecondaryGBuffer[gbufferIndex] = secondaryGBufferData;
    }

    if (outputShadingResult)
    {
        float3 diffuse = isSpecularRay ? 0.0 : radiance * throughput.rgb;
        float3 specular = isSpecularRay ? radiance * throughput.rgb : 0.0;

        specular = DemodulateSpecular(primarySurface.material.specularF0, specular);

        StoreShadingOutput(pixelPosition, diffuse, specular, false);
    }
}
