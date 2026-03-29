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
    if (surface.material.roughness == 0)
        brdf.specular = 0;
    else
        brdf.specular = GGX_times_NdotL(V, L, surface.normal, max(surface.material.roughness, kMinRoughness), surface.material.specularF0);
    return brdf;
}

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
