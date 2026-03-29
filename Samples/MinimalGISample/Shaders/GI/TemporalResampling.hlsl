#pragma pack_matrix(row_major)

#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE RTXDI_SCREEN_SPACE_GROUP_SIZE
#define RTXDI_ENABLE_PRESAMPLING 0

#include "../RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include <Rtxdi/GI/BoilingFilter.hlsli>
#include <Rtxdi/GI/TemporalResampling.hlsli>

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID, uint2 LocalIndex : SV_GroupThreadID)
{
    uint2 pixelPosition = GlobalIndex;

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(GlobalIndex, 7);

    const RAB_Surface primarySurface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_GIReservoir reservoir = RTXDI_LoadGIReservoir(g_Const.restirGI.reservoirBufferParams,
        GlobalIndex, g_Const.restirGI.bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex);

    float3 motionVector = u_MotionVectors[pixelPosition].xyz;
    motionVector = convertMotionVectorToPixelSpace(g_Const.view, g_Const.prevView, pixelPosition, motionVector);

    if (RAB_IsSurfaceValid(primarySurface))
    {
        RTXDI_GITemporalResamplingParameters tParams;

        tParams.screenSpaceMotion = motionVector;
        tParams.sourceBufferIndex = g_Const.restirGI.bufferIndices.temporalResamplingInputBufferIndex;
        tParams.maxHistoryLength = g_Const.restirGI.temporalResamplingParams.maxHistoryLength;
        tParams.biasCorrectionMode = g_Const.restirGI.temporalResamplingParams.temporalBiasCorrectionMode;
        tParams.depthThreshold = g_Const.restirGI.temporalResamplingParams.depthThreshold;
        tParams.normalThreshold = g_Const.restirGI.temporalResamplingParams.normalThreshold;
        tParams.enablePermutationSampling = g_Const.restirGI.temporalResamplingParams.enablePermutationSampling;
        tParams.enableFallbackSampling = g_Const.restirGI.temporalResamplingParams.enableFallbackSampling;
        tParams.uniformRandomNumber = g_Const.restirGI.temporalResamplingParams.uniformRandomNumber;
        tParams.maxReservoirAge = g_Const.restirGI.temporalResamplingParams.maxReservoirAge * (0.5 + RAB_GetNextRandom(rng) * 0.5);

        reservoir = RTXDI_GITemporalResampling(pixelPosition, primarySurface, reservoir, rng,
            g_Const.runtimeParams, g_Const.restirGI.reservoirBufferParams, tParams);
    }

    if (g_Const.restirGI.temporalResamplingParams.enableBoilingFilter)
    {
        RTXDI_GIBoilingFilter(LocalIndex, g_Const.restirGI.temporalResamplingParams.boilingFilterStrength, reservoir);
    }

    RTXDI_StoreGIReservoir(reservoir, g_Const.restirGI.reservoirBufferParams,
        GlobalIndex, g_Const.restirGI.bufferIndices.temporalResamplingOutputBufferIndex);
}
