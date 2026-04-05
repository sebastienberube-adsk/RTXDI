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
//static float4 cDebug;// = float4(0,1,0,1);
#include "RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include <Rtxdi/DI/InitialSampling.hlsli>
#include <Rtxdi/DI/SpatioTemporalResampling.hlsli>

#include "PrimaryRays.hlsli"

/*
 * Call Tree: MinimalSample Render.hlsl::main()
 * ============================================
 *
 * main(uint2 pixelPosition)
 * |
 * +-- TracePrimaryRay(pixelPosition)                          [PrimaryRays.hlsli]
 * |   +-- setupPrimaryRay(pixelPosition, view)                [GBufferHelpers.hlsli]
 * |   +-- RayQuery.TraceRayInline(SceneBVH, ...)              [DXR Intrinsic]
 * |   +-- getGeometryFromHit(...)                             [SceneGeometry.hlsli]
 * |   +-- computeRayIntersectionBarycentrics(...)             [GBufferHelpers.hlsli]
 * |   +-- sampleGeometryMaterial(...)                         [SceneGeometry.hlsli]
 * |   +-- getBentNormal(...)                                  [GBufferHelpers.hlsli]
 * |   +-- getMotionVector(...)                                [GBufferHelpers.hlsli]
 * |   +-- RAB_EmptySurface()                                  [RAB_Surface.hlsli]
 * |   \-- getSurfaceDiffuseProbability(surface)               [RAB_Surface.hlsli]
 * |       +-- RAB_GetMaterial(surface)
 * |       +-- calcLuminance(diffuseAlbedo)
 * |       \-- Schlick_Fresnel(specularF0, NdotV)
 * |
 * +-- [Store G-Buffer data to textures]
 * |
 * +-- RAB_IsSurfaceValid(surface)                             [RAB_Surface.hlsli]
 * |
 * +-- RAB_InitRandomSampler(pixelPosition, pass)              [RAB_RandomSamplerState.hlsli]
 * |   \-- initRandomSampler(index, seed)                      [HelperFunctions.hlsli]
 * |
 * +-- RTXDI_InitSampleParameters(...)                         [RTXDI] 
 * |
 * +-- RTXDI_SampleLocalLights(...)                            [RTXDI] 
 * |
 * +-- RTXDI_CombineDIReservoirs(reservoir, localReservoir)    [RTXDI] 
 * |
 * +-- RTXDI_SampleBrdf(rng, surface, sampleParams, ...)       [RTXDI] 
 * |
 * +-- RTXDI_CombineDIReservoirs(reservoir, brdfReservoir)     [RTXDI] 
 * |
 * +-- RTXDI_FinalizeResampling(reservoir, ...)                [RTXDI] 
 * |
 * +-- RTXDI_IsValidDIReservoir(reservoir)                     [RTXDI] 
 * |
 * +-- RAB_GetConservativeVisibility(surface, lightSample)     [RAB_VisibilityTest.hlsli]
 * |   +-- setupVisibilityRay(surface, lightSample)
 * |   \-- RayQuery.TraceRayInline(SceneBVH, ...)              [DXR Intrinsic]
 * |
 * +-- RTXDI_StoreVisibilityInDIReservoir(reservoir, ...)      [RTXDI] 
 * |
 * +-- [if enableResampling]
 * |   |
 * |   \-- RTXDI_DISpatioTemporalResampling(...)               [RTXDI] 
 * |
 * +-- ShadeSurfaceWithLightSample(lightSample, surface)       [RAB_LightSampling.hlsli]
 * |   +-- normalize(lightSample.position - surface.worldPos)
 * |   +-- dot(L, surface.geoNormal)                           [geometry check]
 * |   +-- Lambert(normal, -L)                                 [BRDF.hlsli]
 * |   +-- GGX_times_NdotL(V, L, N, roughness, specularF0)     [BRDF.hlsli]
 * |   \-- [combine diffuse + specular]
 * |
 * +-- RTXDI_GetDIReservoirInvPdf(reservoir)                   [RTXDI] 
 * |
 * +-- RAB_GetConservativeVisibility(surface, lightSample)     [RAB_VisibilityTest.hlsli]
 * |   \-- (same as above)
 * |
 * +-- RTXDI_StoreVisibilityInDIReservoir(reservoir, ...)      [RTXDI] 
 * |
 * +-- basicToneMapping(shadingOutput, exposure)               [HelperFunctions.hlsli]
 * |
 * \-- RTXDI_StoreDIReservoir(reservoir, params, ...)          [RTXDI] 
 *
 *
 * Legend:
 * -------
 *   +--  : branch continues (more siblings follow)
 *   \--  : last branch (no more siblings)
 *   |    : vertical connector
 *   [RTXDI]  : pure RTXDI library function, branch not expanded
 *
 *
 * Summary by Category:
 * --------------------
 *
 * Application Code (MinimalSample):
 *   - TracePrimaryRay           : Traces camera ray, builds surface
 *   - ShadeSurfaceWithLightSample : BRDF evaluation for final shading
 *   - basicToneMapping          : HDR to LDR conversion
 *
 * RAB Bridge Functions (Application-Implemented):
 *   - RAB_InitRandomSampler     : Initialize RNG
 *   - RAB_IsSurfaceValid        : Check if surface hit geometry
 *   - RAB_EmptySurface          : Create invalid surface
 *   - RAB_GetConservativeVisibility : Shadow ray tracing
 *   - RAB_GetMaterial           : Extract material from surface
 *
 * RTXDI Library Functions (Stopped Branches):
 *   - RTXDI_InitSampleParameters      : Configure sampling counts
 *   - RTXDI_SampleLocalLights         : Initial light sampling with RIS
 *   - RTXDI_SampleBrdf                : BRDF-guided light sampling
 *   - RTXDI_CombineDIReservoirs       : Merge two reservoirs
 *   - RTXDI_FinalizeResampling        : Normalize reservoir weights
 *   - RTXDI_IsValidDIReservoir        : Check if reservoir has valid sample
 *   - RTXDI_DISpatioTemporalResampling: Temporal + spatial reuse
 *   - RTXDI_GetDIReservoirInvPdf      : Get weight for Monte Carlo
 *   - RTXDI_StoreVisibilityInDIReservoir : Cache visibility result
 *   - RTXDI_StoreDIReservoir          : Write reservoir to buffer
 *
 */
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    //cDebug = float4(0,0,0,0);

    /*
    struct RTXDI_LightBufferRegion{
        uint32_t firstLightIndex;
        uint32_t numLights;
    };

    struct RTXDI_EnvironmentLightBufferParameters\{
        uint32_t lightPresent;
        uint32_t lightIndex;
    };

    // Light Type Definitions from Doc/Integration.md:
    //
    // Local Lights:      Lights with a position and finite power        e.g. Point lights, spot lights, area lights, emissive triangles, sphere lights, disk lights
    // Infinite Lights:   Lights at infinite distance with no position   e.g. Directional lights, dome lights
    // Environment Light: HDR environment map (importance sampled)       e.g. Skybox, IBL probe

    struct RTXDI_LightBufferParameters{
        RTXDI_LightBufferRegion localLightBufferRegion;
        RTXDI_LightBufferRegion infiniteLightBufferRegion;
        RTXDI_EnvironmentLightBufferParameters environmentLightParams;
    };
    */
    
    const RTXDI_LightBufferParameters lightBufferParams = g_Const.lightBufferParams;

    // Trace the primary ray
    
    /*
    struct RAB_Material
    {
        float3 diffuseAlbedo;
        float3 specularF0;
        float roughness;
    };
    struct RAB_Surface
    {
        // A surface with enough information to evaluate BRDFs
        float3 worldPos;
        float3 viewDir;
        float viewDepth;
        float3 normal;
        float3 geoNormal;
        float diffuseProbability;
        RAB_Material material;
    };
    struct PrimarySurfaceOutput
    {
        RAB_Surface surface;
        float3 motionVector;
        float3 emissiveColor;
    };
    */
    PrimarySurfaceOutput primary = TracePrimaryRay(pixelPosition);
    //primary.surface.material.diffuseAlbedo = float3(0.1,0.1,0.1);  //Surface color (unlit)
    //primary.surface.material.specularF0 = 1.0; //perpendicular reflectance (1.0=100%)
    //primary.surface.material.roughness = 0.05;


    // ------------------- Normal Debug -------------------
    //cDebug = float4(primary.surface.geoNormal,1);
    //cDebug = float4(primary.surface.normal,1); //Final normal (interpolated geo normal + material normal)

    // ------------------- Depth Debug (linear) -------------------
    //float depthRange = 2.5;
    //float minDepth = 1.7;
    //cDebug = float4((primary.surface.viewDepth-minDepth)/depthRange,0,0,1); //Depth from camera (0=close)
    //cDebug = float4(frac(primary.surface.viewDepth*10.0),0,0,1); //Linear Depth Test (with stripes)

    // Store the G-buffer data for resampling on the next frame
    u_GBufferDepth[pixelPosition] = primary.surface.viewDepth;
    u_GBufferNormals[pixelPosition] = ndirToOctUnorm32(primary.surface.normal);
    u_GBufferGeoNormals[pixelPosition] = ndirToOctUnorm32(primary.surface.geoNormal);
    u_GBufferDiffuseAlbedo[pixelPosition] = Pack_R11G11B10_UFLOAT(primary.surface.material.diffuseAlbedo);
    u_GBufferSpecularRough[pixelPosition] = Pack_R8G8B8A8_Gamma_UFLOAT(float4(primary.surface.material.specularF0, primary.surface.material.roughness));
    
    // This empty reservoir is the accumulator. The accumulation can only be done in "unfinalized" form.
    // Note: When calling RTXDI_CombineDIReservoirs, the accumulator reservoir (1st arg) should be in "unfinalized" form,
    //       and the new reservoir (2nd arg) should be in finalized form.
    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();

    if (RAB_IsSurfaceValid(primary.surface))
    {
        // Initialize the RNG
        // typedef RandomSamplerState RAB_RandomSamplerState;
        /*
        struct RandomSamplerState
        {
            uint seed;
            uint index;
        };
        */
        RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 1);

        //cDebug = float4(0,rng.seed/5000000000.0,0,1);                     //Seed is random in range [0-5000000000]
        //if(g_Const.numInitialSamples == 8 ) cDebug = float4(0,1,0,1);     //g_Const.numInitialSamples == 8
        //if(g_Const.numInitialBRDFSamples == 1 ) cDebug = float4(1,1,0,1); //g_Const.numInitialSamples == 1
        
        /*
        struct RTXDI_SampleParameters
        {    
            uint numLocalLightSamples;
            uint numInfiniteLightSamples;
            uint numEnvironmentMapSamples;
            uint numBrdfSamples;

            uint numMisSamples;
            float localLightMisWeight;
            float environmentMapMisWeight;
            float brdfMisWeight;
            float brdfCutoff;
            float brdfRayMinT;
        };
        */
        RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(
            g_Const.numInitialSamples, // local light samples 
            0, // infinite light samples
            0, // environment map samples
            g_Const.numInitialBRDFSamples,
            g_Const.brdfCutoff,
            0.001f);

        // Generate the initial sample
        /*
        struct RAB_LightSample
        {
            float3 position;
            float3 normal;
            float3 radiance;
            float solidAnglePdf;
        };*/
        RAB_LightSample lightSample = RAB_EmptyLightSample();

        //-----------------------------------------------------------------------
        //|                    LOCAL LIGHT SAMPLING                             |
        //|                    (numInitialSamples)                              |
        //|                                                                     |
        //|     Light A -----|                                                  |
        //|     Light B -----|--> Pick random lights --> Check if visible       |
        //|     Light C -----|    from buffer            to surface             |
        //|     Light D -----|                                                  |
        //|                                                                     |
        //|  + Fast (no ray tracing per sample)                                 |
        //|  - May pick lights that don't contribute (occluded, wrong side)     |
        //-----------------------------------------------------------------------
        RTXDI_DIReservoir localReservoir = RTXDI_SampleLocalLights(rng, rng, primary.surface,
            sampleParams, ReSTIRDI_LocalLightSamplingMode_UNIFORM, lightBufferParams.localLightBufferRegion, lightSample);

        // This is seemingly necessary to "undo" the finalization step done inside RTXDI_SampleLocalLights,
        // which divides the reservoir weightSum by the number of samples considered (M).
        // So this step is essentially converting the reservoir back to "unfinalized" form
        // into the accumulator reservoir.
        RTXDI_CombineDIReservoirs(reservoir, localReservoir, 0.5, localReservoir.targetPdf);

        // Resample BRDF samples.
        RAB_LightSample brdfSample = RAB_EmptyLightSample();

        //-----------------------------------------------------------------------
        //|                         BRDF SAMPLING                               |
        //|                         (numInitialBRDFSamples)                     |
        //|                                                                     |
        //|                        / Ray 1 -> hits Light A                      |
        //|     Surface ----------/  Ray 2 -> hits nothing                      |
        //|     (shiny)           \  Ray 3 -> hits Light C                      |
        //|                        \                                            |
        //|                                                                     |
        //|  + Finds lights the surface "wants" (follows BRDF importance)       |
        //|  + Great for specular/glossy surfaces                               |
        //|  - Expensive (requires ray tracing per sample)                      |
        //-----------------------------------------------------------------------
        // Note: The returned reservoir is already in "finalized" form, meaning the weightSum is already divided by the number of samples (M).
        RTXDI_DIReservoir brdfReservoir = RTXDI_SampleBrdf(rng, primary.surface, sampleParams, lightBufferParams, brdfSample);
        
        // Note: reservoir is the accumulator reservoir (unfinalized), brdfReservoir is the new reservoir (finalized).
        // This follows the same pattern as the combination after local light sampling, where we combine the finalized BRD
        // reservoir with the accumulator reservoir.
        bool selectBrdf = RTXDI_CombineDIReservoirs(reservoir, brdfReservoir, RAB_GetNextRandom(rng), brdfReservoir.targetPdf);
        if (selectBrdf)
        {
            lightSample = brdfSample;
        }

        /*
        // Performs normalization of the reservoir after streaming. Equation (6) from the ReSTIR paper.
        void RTXDI_FinalizeResampling(
            inout RTXDI_DIReservoir reservoir,
            float normalizationNumerator,
            float normalizationDenominator)
        {
            float denominator = reservoir.targetPdf * normalizationDenominator;

            reservoir.weightSum = (denominator == 0.0) ? 0.0 : (reservoir.weightSum * normalizationNumerator) / denominator;
        }
        */
        RTXDI_FinalizeResampling(reservoir, 1.0, 1.0);
        reservoir.M = 1;
         
        // BRDF was generated with a trace so no need to trace visibility again
        if (RTXDI_IsValidDIReservoir(reservoir) && !selectBrdf)
        {
            // See if the initial sample is visible from the surface
            if (!RAB_GetConservativeVisibility(primary.surface, lightSample))
            {
                // If not visible, discard the sample (but keep the M)
                RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
            }
        }

        // Apply spatio-temporal resampling, if enabled
        // NOTE: This is an exposed setting in the UI.
        // When disabled, lighting effect is really sparse and noisy, mostly black.
        if (g_Const.enableResampling)
        {
            //cDebug = float4(0,1,0,1); //Tested: g_Const.enableResampling = true

            // Fill out the parameter structure.
            // Mostly use literal constants for simplicity.
            RTXDI_DISpatioTemporalResamplingParameters stparams;
            stparams.screenSpaceMotion = primary.motionVector;
            stparams.sourceBufferIndex = g_Const.inputBufferIndex;
            stparams.maxHistoryLength = 20;
            stparams.biasCorrectionMode = g_Const.unbiasedMode ? RTXDI_BIAS_CORRECTION_RAY_TRACED : RTXDI_BIAS_CORRECTION_BASIC;
            stparams.depthThreshold = 0.1;
            stparams.normalThreshold = 0.5;
            stparams.numSamples = g_Const.numSpatialSamples + 1;
            stparams.numDisocclusionBoostSamples = 0;
            stparams.samplingRadius = 32;
            stparams.enableVisibilityShortcut = true;
            stparams.enablePermutationSampling = true;
            stparams.discountNaiveSamples = false;

            // This variable will receive the position of the sample reused from the previous frame.
            // It's only needed for gradient evaluation, ignore it here.
            int2 temporalSamplePixelPos = -1;

            // Call the resampling function, update the reservoir and lightSample variables
            reservoir = RTXDI_DISpatioTemporalResampling(pixelPosition, primary.surface, reservoir,
                    rng, g_Const.runtimeParams, g_Const.restirDIReservoirBufferParams, stparams, temporalSamplePixelPos, lightSample);
        }

        float3 shadingOutput = 0;

        // Shade the surface with the selected light sample
        if (RTXDI_IsValidDIReservoir(reservoir))
        {
            // Compute the correctly weighted reflected radiance
            shadingOutput = ShadeSurfaceWithLightSample(lightSample, primary.surface)
                          * RTXDI_GetDIReservoirInvPdf(reservoir);

            // Test if the selected light is visible from the surface
            bool visibility = RAB_GetConservativeVisibility(primary.surface, lightSample);

            // If not visible, discard the shading output and the light sample
            if (!visibility)
            {
                shadingOutput = 0;
                RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
            }
        }

        // Compositing and tone mapping
        shadingOutput += primary.emissiveColor;
        shadingOutput = basicToneMapping(shadingOutput, 0.005);

        u_ShadingOutput[pixelPosition] = float4(shadingOutput, 1.0);
    }
    else
    {
        // No valid surface
        u_ShadingOutput[pixelPosition] = 0;
    }

    RTXDI_StoreDIReservoir(reservoir, g_Const.restirDIReservoirBufferParams, pixelPosition, g_Const.outputBufferIndex);
}