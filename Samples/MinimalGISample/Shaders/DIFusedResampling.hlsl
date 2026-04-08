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
#include <Rtxdi/DI/SpatioTemporalResampling.hlsli>

#include "ShadingHelpers.hlsli"

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    const RTXDI_RuntimeParameters params = g_Const.runtimeParams;
    const RTXDI_LightBufferParameters lightBufferParams = g_Const.lightBufferParams;

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 1);
    RAB_RandomSamplerState tileRng = RAB_InitRandomSampler(pixelPosition / RTXDI_TILE_SIZE_IN_PIXELS, 1);

    RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(
        g_Const.restirDI.initialSamplingParams.numPrimaryLocalLightSamples,
        0, 0,
        g_Const.restirDI.initialSamplingParams.numPrimaryBrdfSamples,
        g_Const.restirDI.initialSamplingParams.brdfCutoff,
        0.001f);

    RAB_LightSample lightSample = RAB_EmptyLightSample();
    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();

    if (RAB_IsSurfaceValid(surface))
    {
        reservoir = RTXDI_SampleLightsForSurface(rng, tileRng, surface,
            sampleParams, lightBufferParams, ReSTIRDI_LocalLightSamplingMode_UNIFORM,
            lightSample);

        if (g_Const.restirDI.initialSamplingParams.enableInitialVisibility && RTXDI_IsValidDIReservoir(reservoir))
        {
            if (!RAB_GetConservativeVisibility(surface, lightSample))
            {
                RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
            }
        }

        float3 motionVector = u_MotionVectors[pixelPosition].xyz;

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
        stparams.enablePermutationSampling = g_Const.restirDI.temporalResamplingParams.enablePermutationSampling;
        stparams.enableMaterialSimilarityTest = g_Const.enableMaterialSimilarityTest;
        stparams.uniformRandomNumber = g_Const.restirDI.temporalResamplingParams.uniformRandomNumber;
        stparams.discountNaiveSamples = false;

        int2 temporalSamplePixelPos = -1;

        reservoir = RTXDI_DISpatioTemporalResampling(pixelPosition, surface, reservoir,
            rng, params, g_Const.restirDI.reservoirBufferParams, stparams,
            temporalSamplePixelPos, lightSample);
    }

    float3 diffuse = 0;
    float3 specular = 0;

    if (RAB_IsSurfaceValid(surface) && RTXDI_IsValidDIReservoir(reservoir))
    {
        RAB_LightInfo lightInfo = RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(reservoir), false);
        lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, RTXDI_GetDIReservoirSampleUV(reservoir));

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
            }
        }
    }

    RTXDI_StoreDIReservoir(reservoir, g_Const.restirDI.reservoirBufferParams,
        pixelPosition, g_Const.restirDI.bufferIndices.shadingInputBufferIndex);

    StoreShadingOutput(pixelPosition, diffuse, specular, true);
}
