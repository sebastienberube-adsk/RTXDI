#pragma pack_matrix(row_major)

#define RAB_ENABLE_SPECULAR_MIS 0
#define RTXDI_ENABLE_PRESAMPLING 0

#include "RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include <Rtxdi/DI/InitialSampling.hlsli>
#include <Rtxdi/GI/Reservoir.hlsli>

#include "ShadingHelpers.hlsli"

static const float c_MaxIndirectRadiance = 10;

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    uint2 pixelPosition = GlobalIndex;

    if (any(pixelPosition >= int2(g_Const.view.viewportSize)))
        return;

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(GlobalIndex, 6);
    RAB_RandomSamplerState tileRng = RAB_InitRandomSampler(GlobalIndex / RTXDI_TILE_SIZE_IN_PIXELS, 1);

    RAB_Surface primarySurface = RAB_GetGBufferSurface(pixelPosition, false);

    const uint gbufferIndex = RTXDI_ReservoirPositionToPointer(g_Const.restirGI.reservoirBufferParams, GlobalIndex, 0);
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
            0, 0, 0, 0.0f, 0.0f);

        RAB_LightSample lightSample;
        RTXDI_DIReservoir reservoir = RTXDI_SampleLightsForSurface(rng, tileRng, secondarySurface,
            sampleParams, g_Const.lightBufferParams,
            g_Const.restirDI.initialSamplingParams.localLightSamplingMode,
            lightSample);

        float3 indirectDiffuse = 0;
        float3 indirectSpecular = 0;
        if (lightSample.solidAnglePdf > 0)
        {
            SplitBrdf brdf = EvaluateBrdf(secondarySurface, lightSample.position);
            float3 lightRadiance = lightSample.radiance * RTXDI_GetDIReservoirInvPdf(reservoir)
                                 / lightSample.solidAnglePdf;

            bool visibility = RAB_GetConservativeVisibility(secondarySurface, lightSample);
            if (!visibility)
                lightRadiance = 0;

            indirectDiffuse = brdf.demodulatedDiffuse * lightRadiance;
            indirectSpecular = brdf.specular * lightRadiance;
        }

        radiance += indirectDiffuse * secondarySurface.material.diffuseAlbedo + indirectSpecular;

        float indirectLuminance = calcLuminance(radiance);
        if (indirectLuminance > c_MaxIndirectRadiance)
            radiance *= c_MaxIndirectRadiance / indirectLuminance;
    }

    bool outputShadingResult = isSpecularRay && isDeltaSurface;

    RTXDI_GIReservoir giReservoir = RTXDI_EmptyGIReservoir();

    if (isValidSecondarySurface && !outputShadingResult)
    {
        giReservoir = RTXDI_MakeGIReservoir(secondarySurface.worldPos,
            secondarySurface.normal, radiance, secondaryGBufferData.pdf);
    }

    RTXDI_StoreGIReservoir(giReservoir, g_Const.restirGI.reservoirBufferParams,
        GlobalIndex, g_Const.restirGI.bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex);

    secondaryGBufferData.emission = outputShadingResult ? 0 : radiance;
    u_SecondaryGBuffer[gbufferIndex] = secondaryGBufferData;

    if (outputShadingResult)
    {
        float3 diffuse = isSpecularRay ? 0.0 : radiance * throughput;
        float3 specular = isSpecularRay ? radiance * throughput : 0.0;

        specular = DemodulateSpecular(primarySurface.material.specularF0, specular);

        StoreShadingOutput(pixelPosition, diffuse, specular, false);
    }
}
