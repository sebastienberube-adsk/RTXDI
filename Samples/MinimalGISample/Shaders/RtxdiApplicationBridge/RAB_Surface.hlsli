#ifndef RTXDI_RAB_SURFACE_HLSLI
#define RTXDI_RAB_SURFACE_HLSLI

#include "../GBufferHelpers.hlsli"

#include "RAB_RandomSamplerState.hlsli"
#include "RAB_Material.hlsli"

// A surface with enough information to evaluate BRDFs
struct RAB_Surface
{
    float3 worldPos;
    float3 viewDir;
    float viewDepth;
    float3 normal;
    float3 geoNormal;
    float diffuseProbability;
    RAB_Material material;
};

RAB_Surface RAB_EmptySurface()
{
    RAB_Surface surface = (RAB_Surface)0;
    surface.viewDepth = BACKGROUND_DEPTH;
    return surface;
}

bool RAB_IsSurfaceValid(RAB_Surface surface)
{
    return surface.viewDepth != BACKGROUND_DEPTH;
}

float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)
{
    return surface.worldPos;
}

RAB_Material RAB_GetMaterial(RAB_Surface surface)
{
    return surface.material;
}

float3 RAB_GetSurfaceNormal(RAB_Surface surface)
{
    return surface.normal;
}

float RAB_GetSurfaceLinearDepth(RAB_Surface surface)
{
    return surface.viewDepth;
}

float getSurfaceDiffuseProbability(RAB_Surface surface)
{
    RAB_Material material = RAB_GetMaterial(surface);
    float diffuseWeight = calcLuminance(material.diffuseAlbedo);
    float specularWeight = calcLuminance(Schlick_Fresnel(material.specularF0, dot(surface.viewDir, surface.normal)));
    float sumWeights = diffuseWeight + specularWeight;
    return sumWeights < 1e-7f ? 1.f : (diffuseWeight / sumWeights);
}

RAB_Surface RAB_GetGBufferSurface(int2 pixelPosition, bool previousFrame)
{
    RAB_Surface surface = RAB_EmptySurface();

    if (previousFrame)
    {
        const PlanarViewConstants view = g_Const.prevView;

        if (any(pixelPosition >= view.viewportSize))
            return surface;

        surface.viewDepth = t_PrevGBufferDepth[pixelPosition];

        if(surface.viewDepth == BACKGROUND_DEPTH)
            return surface;

        surface.normal = octToNdirUnorm32(t_PrevGBufferNormals[pixelPosition]);
        surface.geoNormal = octToNdirUnorm32(t_PrevGBufferGeoNormals[pixelPosition]);

        surface.material.diffuseAlbedo = Unpack_R11G11B10_UFLOAT(t_PrevGBufferDiffuseAlbedo[pixelPosition]).rgb;
        float4 specularRough = Unpack_R8G8B8A8_Gamma_UFLOAT(t_PrevGBufferSpecularRough[pixelPosition]);
        surface.material.roughness = specularRough.a;
        surface.material.specularF0 = specularRough.rgb;

        surface.worldPos = viewDepthToWorldPos(view, pixelPosition, surface.viewDepth);
        surface.viewDir = normalize(g_Const.prevView.cameraDirectionOrPosition.xyz - surface.worldPos);
        surface.diffuseProbability = getSurfaceDiffuseProbability(surface);
    }
    else
    {
        const PlanarViewConstants view = g_Const.view;

        if (any(pixelPosition >= view.viewportSize))
            return surface;

        surface.viewDepth = u_GBufferDepth[pixelPosition];

        if(surface.viewDepth == BACKGROUND_DEPTH)
            return surface;

        surface.normal = octToNdirUnorm32(u_GBufferNormals[pixelPosition]);
        surface.geoNormal = octToNdirUnorm32(u_GBufferGeoNormals[pixelPosition]);

        surface.material.diffuseAlbedo = Unpack_R11G11B10_UFLOAT(u_GBufferDiffuseAlbedo[pixelPosition]).rgb;
        float4 specularRough = Unpack_R8G8B8A8_Gamma_UFLOAT(u_GBufferSpecularRough[pixelPosition]);
        surface.material.roughness = specularRough.a;
        surface.material.specularF0 = specularRough.rgb;

        surface.worldPos = viewDepthToWorldPos(view, pixelPosition, surface.viewDepth);
        surface.viewDir = normalize(view.cameraDirectionOrPosition.xyz - surface.worldPos);
        surface.diffuseProbability = getSurfaceDiffuseProbability(surface);
    }

    return surface;
}

float3 worldToTangent(RAB_Surface surface, float3 w)
{
    // reconstruct tangent frame based off worldspace normal
    // this is ok for isotropic BRDFs
    // for anisotropic BRDFs, we need a user defined tangent
    float3 tangent;
    float3 bitangent;
    ConstructONB(surface.normal, tangent, bitangent);

    return float3(dot(bitangent, w), dot(tangent, w), dot(surface.normal, w));
}

float3 tangentToWorld(RAB_Surface surface, float3 h)
{
    // reconstruct tangent frame based off worldspace normal
    // this is ok for isotropic BRDFs
    // for anisotropic BRDFs, we need a user defined tangent
    float3 tangent;
    float3 bitangent;
    ConstructONB(surface.normal, tangent, bitangent);

    return bitangent * h.x + tangent * h.y + surface.normal * h.z;
}

// Output an importanced sampled reflection direction from the BRDF given the view
// Return true if the returned direction is above the surface
bool RAB_GetSurfaceBrdfSample(RAB_Surface surface, inout RAB_RandomSamplerState rng, out float3 dir)
{
    float3 rand;
    rand.x = RAB_GetNextRandom(rng);
    rand.y = RAB_GetNextRandom(rng);
    rand.z = RAB_GetNextRandom(rng);
    if (rand.x < surface.diffuseProbability)
    {
        float pdf;
        float3 h = SampleCosHemisphere(rand.yz, pdf);
        dir = tangentToWorld(surface, h);
    }
    else
    {
        float3 Ve = normalize(worldToTangent(surface, surface.viewDir));
        float3 h = ImportanceSampleGGX_VNDF(rand.yz, max(surface.material.roughness, kMinRoughness), Ve, 1.0);
        h = normalize(h);
        dir = reflect(-surface.viewDir, tangentToWorld(surface, h));
    }

    return dot(surface.normal, dir) > 0.f;
}

// Return PDF wrt solid angle for the BRDF in the given dir
float RAB_GetSurfaceBrdfPdf(RAB_Surface surface, float3 dir)
{
    float cosTheta = saturate(dot(surface.normal, dir));
    float diffusePdf = cosTheta / M_PI;
    float specularPdf = ImportanceSampleGGX_VNDF_PDF(max(surface.material.roughness, kMinRoughness), surface.normal, surface.viewDir, dir);
    float pdf = cosTheta > 0.f ? lerp(specularPdf, diffusePdf, surface.diffuseProbability) : 0.f;
    return pdf;
}

#endif // RTXDI_RAB_SURFACE_HLSLI
