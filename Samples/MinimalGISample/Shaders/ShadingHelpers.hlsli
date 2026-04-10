#ifndef SHADING_HELPERS_HLSLI
#define SHADING_HELPERS_HLSLI

struct SplitBrdf
{
    float demodulatedDiffuse;
    float3 specular;
};

SplitBrdf EvaluateBrdf(RAB_Surface surface, float3 samplePosition)
{
    float3 V = surface.viewDir;
    float3 L = normalize(samplePosition - surface.worldPos);

    SplitBrdf brdf;
    brdf.demodulatedDiffuse = Lambert(surface.normal, -L);
    // Guard roughness==0 to avoid degenerate GGX evaluation and align with
    // corresponding minimal/intermediate compatibility behavior.
    if (surface.material.roughness == 0)
        brdf.specular = 0;
    else
        brdf.specular = GGX_times_NdotL(V, L, surface.normal, max(surface.material.roughness, kMinRoughness), surface.material.specularF0);
    return brdf;
}

#ifdef RTXDI_DIRESERVOIR_HLSLI

bool ShadeSurfaceWithLightSample(
    inout RTXDI_DIReservoir reservoir,
    RAB_Surface surface,
    RAB_LightSample lightSample,
    bool enableVisibilityReuse,
    out float3 diffuse,
    out float3 specular)
{
    diffuse = 0;
    specular = 0;

    if (lightSample.solidAnglePdf <= 0)
        return false;

    bool needToStore = false;
    if (g_Const.restirDI.shadingParams.enableFinalVisibility)
    {
        float3 visibility = 0;
        bool visibilityReused = false;

        if (g_Const.restirDI.shadingParams.reuseFinalVisibility && enableVisibilityReuse)
        {
            RTXDI_VisibilityReuseParameters rparams;
            rparams.maxAge = g_Const.restirDI.shadingParams.finalVisibilityMaxAge;
            rparams.maxDistance = g_Const.restirDI.shadingParams.finalVisibilityMaxDistance;

            visibilityReused = RTXDI_GetDIReservoirVisibility(reservoir, rparams, visibility);
        }

        if (!visibilityReused)
        {
            visibility = GetFinalVisibility(SceneBVH, surface, lightSample.position);
            // Always persist evaluated visibility into the reservoir so temporal
            // consumers see the latest result, matching IntermediateSample flow.
            RTXDI_StoreVisibilityInDIReservoir(reservoir, visibility, g_Const.restirDI.temporalResamplingParams.discardInvisibleSamples);
            needToStore = true;
        }

        lightSample.radiance *= visibility;
    }

    lightSample.radiance *= RTXDI_GetDIReservoirInvPdf(reservoir) / lightSample.solidAnglePdf;

    if (any(lightSample.radiance > 0))
    {
        float3 L = normalize(lightSample.position - surface.worldPos);
        // Reject samples below the geometric normal (same criterion as MinimalSample path).
        if (dot(L, surface.geoNormal) > 0)
        {
            SplitBrdf brdf = EvaluateBrdf(surface, lightSample.position);

            diffuse = brdf.demodulatedDiffuse * lightSample.radiance;
            specular = brdf.specular * lightSample.radiance;
        }
    }

    return needToStore;
}

#endif // RTXDI_DIRESERVOIR_HLSLI

float3 DemodulateSpecular(float3 surfaceSpecularF0, float3 specular)
{
    return specular / max(0.01, surfaceSpecularF0);
}

void StoreShadingOutput(
    uint2 pixelPosition,
    float3 diffuse,
    float3 specular,
    bool isFirstPass)
{
    if (!isFirstPass)
    {
        float4 priorDiffuse = u_DiffuseLighting[pixelPosition];
        float4 priorSpecular = u_SpecularLighting[pixelPosition];
        diffuse += priorDiffuse.rgb;
        specular += priorSpecular.rgb;
    }

    u_DiffuseLighting[pixelPosition] = float4(diffuse, 0);
    u_SpecularLighting[pixelPosition] = float4(specular, 0);
}

#endif // SHADING_HELPERS_HLSLI
