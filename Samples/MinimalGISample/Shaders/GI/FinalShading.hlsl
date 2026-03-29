#pragma pack_matrix(row_major)

#define RTXDI_ENABLE_PRESAMPLING 0

#include "../RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"
#include "../ShadingHelpers.hlsli"

#include <Rtxdi/GI/Reservoir.hlsli>

float3 GetGIFinalVisibility(RAB_Surface surface, float3 samplePosition)
{
    float3 toSample = samplePosition - surface.worldPos;
    float dist = length(toSample);
    float3 dir = toSample / dist;

    RayDesc ray;
    ray.Origin = surface.worldPos;
    ray.Direction = dir;
    ray.TMin = 0.01 * max(1, 0.1 * length(surface.worldPos - g_Const.view.cameraDirectionOrPosition.xyz));
    ray.TMax = max(0, dist - ray.TMin);

    RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rayQuery;
    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_OPAQUE, ray);
    rayQuery.Proceed();

    return (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
}

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    uint2 pixelPosition = GlobalIndex;

    if (any(pixelPosition >= int2(g_Const.view.viewportSize)))
        return;

    const RAB_Surface primarySurface = RAB_GetGBufferSurface(pixelPosition, false);

    const RTXDI_GIReservoir reservoir = RTXDI_LoadGIReservoir(g_Const.restirGI.reservoirBufferParams,
        GlobalIndex, g_Const.restirGI.bufferIndices.finalShadingInputBufferIndex);

    float3 diffuse = 0;
    float3 specular = 0;

    if (RTXDI_IsValidGIReservoir(reservoir))
    {
        float3 radiance = reservoir.radiance * reservoir.weightSum;

        if (g_Const.restirGI.finalShadingParams.enableFinalVisibility)
        {
            float3 visibility = GetGIFinalVisibility(primarySurface, reservoir.position);
            radiance *= visibility;
        }

        const SplitBrdf brdf = EvaluateBrdf(primarySurface, reservoir.position);

        diffuse = brdf.demodulatedDiffuse * radiance;
        specular = brdf.specular * radiance;

        specular = DemodulateSpecular(primarySurface.material.specularF0, specular);
    }

    StoreShadingOutput(pixelPosition, diffuse, specular, false);
}
