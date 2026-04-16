#ifndef RAB_LIGHT_SAMPLING_HLSLI
#define RAB_LIGHT_SAMPLING_HLSLI

#include "RAB_Material.hlsli"
#include "RAB_RayPayload.hlsli"
#include "RAB_Surface.hlsli"

float2 RAB_GetEnvironmentMapRandXYFromDir(float3 worldDir)
{
    float2 uv = directionToEquirectUV(worldDir); 
    uv.x -= g_Const.sceneConstants.environmentRotation;
    uv = frac(uv);
    return uv;
}

float RAB_EvaluateEnvironmentMapSamplingPdf(float3 L)
{
    if (!g_Const.restirDI.initialSamplingParams.environmentMapImportanceSampling)
        return 1.0;

    float2 uv = RAB_GetEnvironmentMapRandXYFromDir(L);

    uint2 pdfTextureSize = g_Const.environmentPdfTextureSize.xy;
    uint2 texelPosition = uint2(pdfTextureSize * uv);
    float texelValue = t_EnvironmentPdfTexture[texelPosition].r;
    
    int lastMipLevel = max(0, int(floor(log2(max(pdfTextureSize.x, pdfTextureSize.y)))));
    float averageValue = t_EnvironmentPdfTexture.mips[lastMipLevel][uint2(0, 0)].x;
    
    float sum = averageValue * square(1u << lastMipLevel);

    return texelValue / sum;
}

float RAB_EvaluateLocalLightSourcePdf(uint lightIndex)
{
    return 1.0 / g_Const.lightBufferParams.localLightBufferRegion.numLights;
}

float3 RAB_GetReflectedRadianceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    float3 L = normalize(incomingRadianceLocation - surface.worldPos);

    if (dot(L, surface.geoNormal) <= 0)
        return 0;

    float d = Lambert(surface.normal, -L);
    float3 s;
    if (surface.material.roughness == 0)
        s = 0;
    else
        s = GGX_times_NdotL(surface.viewDir, L, surface.normal,
            max(surface.material.roughness, kMinRoughness), surface.material.specularF0);

    return incomingRadiance * (d * surface.material.diffuseAlbedo + s);
}

float RAB_GetReflectedLuminanceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    return RTXDI_Luminance(RAB_GetReflectedRadianceForSurface(incomingRadianceLocation, incomingRadiance, surface));
}

float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface)
{
    if (lightSample.solidAnglePdf <= 0)
        return 0;
    
    return RAB_GetReflectedLuminanceForSurface(lightSample.position, lightSample.radiance, surface) / lightSample.solidAnglePdf;
}

float RAB_GetGISampleTargetPdfForSurface(float3 samplePosition, float3 sampleRadiance, RAB_Surface surface)
{
    return RTXDI_Luminance(RAB_GetReflectedRadianceForSurface(samplePosition, sampleRadiance, surface));
}

void RAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample lightSample,
    out float3 o_lightDir,
    out float o_lightDistance)
{
    if (lightSample.lightType == PolymorphicLightType::kEnvironment)
    {
        o_lightDir = -lightSample.normal;
        o_lightDistance = DISTANT_LIGHT_DISTANCE;
    }
    else
    {
        float3 toLight = lightSample.position - surface.worldPos;
        o_lightDistance = length(toLight);
        o_lightDir = toLight / o_lightDistance;
    }
}

bool RTXDI_CompareRelativeDifference(float reference, float candidate, float threshold);

float3 GetEnvironmentRadiance(float3 direction)
{
    if (!g_Const.sceneConstants.enableEnvironmentMap)
        return 0;

    Texture2D environmentLatLongMap = t_BindlessTextures[g_Const.sceneConstants.environmentMapTextureIndex];

    float2 uv = directionToEquirectUV(direction);
    uv.x -= g_Const.sceneConstants.environmentRotation;

    float3 environmentRadiance = environmentLatLongMap.SampleLevel(s_EnvironmentSampler, uv, 0).rgb;
    environmentRadiance *= g_Const.sceneConstants.environmentScale;

    return environmentRadiance;
}

bool IsComplexSurface(int2 pixelPosition, RAB_Surface surface)
{
    return true;
}

uint getLightIndex(uint instanceID, uint geometryIndex, uint primitiveIndex)
{
    uint lightIndex = RTXDI_InvalidLightIndex;
    InstanceData hitInstance = t_InstanceData[instanceID];
    uint geometryInstanceIndex = hitInstance.firstGeometryInstanceIndex + geometryIndex;
    lightIndex = t_GeometryInstanceToLight[geometryInstanceIndex];
    if (lightIndex != RTXDI_InvalidLightIndex)
      lightIndex += primitiveIndex;
    return lightIndex;
}

bool RAB_TraceRayForLocalLight(float3 origin, float3 direction, float tMin, float tMax,
    out uint o_lightIndex, out float2 o_randXY)
{
    o_lightIndex = RTXDI_InvalidLightIndex;
    o_randXY = 0;

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = tMin;
    ray.TMax = tMax;

    RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_OPAQUE, ray);
    rayQuery.Proceed();

    bool hitAnything = rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    if (hitAnything)
    {
        o_lightIndex = getLightIndex(rayQuery.CommittedInstanceID(), rayQuery.CommittedGeometryIndex(), rayQuery.CommittedPrimitiveIndex());
        if (o_lightIndex != RTXDI_InvalidLightIndex)
        {
            float2 hitUV = rayQuery.CommittedTriangleBarycentrics();
            o_randXY = randomFromBarycentric(hitUVToBarycentric(hitUV));
        }
    }

    return hitAnything;
}

#endif // RAB_LIGHT_SAMPLING_HLSLI
