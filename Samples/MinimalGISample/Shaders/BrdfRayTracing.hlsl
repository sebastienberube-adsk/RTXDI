#pragma pack_matrix(row_major)

#define RTXDI_ENABLE_PRESAMPLING 0

#include "RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"
#include "ShadingHelpers.hlsli"

#include <Rtxdi/DI/Reservoir.hlsli>

void buildONB(in float3 n, out float3 b1, out float3 b2)
{
    float s = n.z >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (s + n.z);
    float b = n.x * n.y * a;
    b1 = float3(1.0f + s * n.x * n.x * a, s * b, -s * n.x);
    b2 = float3(b, s + n.y * n.y * a, -n.y);
}

static const float c_MaxIndirectRadiance = 10;

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    uint2 pixelPosition = GlobalIndex;

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    if (!RAB_IsSurfaceValid(surface))
        return;

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(GlobalIndex, 5);

    float3 tangent, bitangent;
    buildONB(surface.normal, tangent, bitangent);

    float dist = max(1, 0.1 * length(surface.worldPos - g_Const.view.cameraDirectionOrPosition.xyz));

    RayDesc ray;
    ray.TMin = 0.001f * dist;
    ray.TMax = 1000;

    float2 Rand;
    Rand.x = RAB_GetNextRandom(rng);
    Rand.y = RAB_GetNextRandom(rng);

    float3 V = normalize(g_Const.view.cameraDirectionOrPosition.xyz - surface.worldPos);

    bool isSpecularRay = false;
    bool isDeltaSurface = surface.material.roughness == 0;
    float specular_PDF;
    float3 BRDF_over_PDF;
    float overall_PDF;

    {
        float3 specularDirection;
        float3 specular_BRDF_over_PDF;
        {
            float3 Ve = float3(dot(V, tangent), dot(V, bitangent), dot(V, surface.normal));
            float3 He = ImportanceSampleGGX_VNDF(Rand, max(surface.material.roughness, 0.01), Ve, 1.0);
            float3 H = isDeltaSurface ? surface.normal : normalize(He.x * tangent + He.y * bitangent + He.z * surface.normal);
            specularDirection = reflect(-V, H);

            float HoV = saturate(dot(H, V));
            float NoV = saturate(dot(surface.normal, V));
            float3 F = Schlick_Fresnel(surface.material.specularF0, HoV);
            float G1 = isDeltaSurface ? 1.0 : (NoV > 0) ? G1_Smith(surface.material.roughness, NoV) : 0;
            specular_BRDF_over_PDF = F * G1;
        }

        float3 diffuseDirection;
        float diffuse_BRDF_over_PDF;
        {
            float solidAnglePdf;
            float3 localDirection = SampleCosHemisphere(Rand, solidAnglePdf);
            diffuseDirection = tangent * localDirection.x + bitangent * localDirection.y + surface.normal * localDirection.z;
            diffuse_BRDF_over_PDF = 1.0;
        }

        specular_PDF = saturate(calcLuminance(specular_BRDF_over_PDF) /
            calcLuminance(specular_BRDF_over_PDF + diffuse_BRDF_over_PDF * surface.material.diffuseAlbedo));

        isSpecularRay = RAB_GetNextRandom(rng) < specular_PDF;

        if (isSpecularRay)
        {
            ray.Direction = specularDirection;
            BRDF_over_PDF = specular_BRDF_over_PDF / specular_PDF;
        }
        else
        {
            ray.Direction = diffuseDirection;
            BRDF_over_PDF = diffuse_BRDF_over_PDF / (1.0 - specular_PDF);
        }

        const float specularLobe_PDF = ImportanceSampleGGX_VNDF_PDF(surface.material.roughness, surface.normal, V, ray.Direction);
        const float diffuseLobe_PDF = saturate(dot(ray.Direction, surface.normal)) / M_PI;

        overall_PDF = isDeltaSurface ? diffuseLobe_PDF : lerp(diffuseLobe_PDF, specularLobe_PDF, specular_PDF);
    }

    if (dot(surface.geoNormal, ray.Direction) <= 0.0)
    {
        BRDF_over_PDF = 0.0;
        ray.TMax = 0;
    }

    ray.Origin = surface.worldPos;

    float3 radiance = 0;
    float committedRayT = 0;
    uint hitInstanceID = ~0u;

    {
        RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
        rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_OPAQUE, ray);
        rayQuery.Proceed();

        if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            hitInstanceID = rayQuery.CommittedInstanceID();
            committedRayT = rayQuery.CommittedRayT();

            GeometrySample gs = getGeometryFromHit(
                rayQuery.CommittedInstanceID(),
                rayQuery.CommittedGeometryIndex(),
                rayQuery.CommittedPrimitiveIndex(),
                rayQuery.CommittedTriangleBarycentrics(),
                GeomAttr_Normal | GeomAttr_TexCoord | GeomAttr_Position,
                t_InstanceData, t_GeometryData, t_MaterialConstants);

            MaterialSample ms = sampleGeometryMaterial(gs, 0, 0, 0,
                MatAttr_BaseColor | MatAttr_Emissive | MatAttr_MetalRough, s_MaterialSampler);

            ms.shadingNormal = getBentNormal(gs.flatNormal, ms.shadingNormal, ray.Direction);

            radiance = ms.emissiveColor;

            SecondaryGBufferData secondaryGBufferData = (SecondaryGBufferData)0;
            secondaryGBufferData.worldPos = ray.Origin + ray.Direction * committedRayT;
            secondaryGBufferData.normal = ndirToOctUnorm32((dot(gs.geometryNormal, ray.Direction) < 0) ? gs.geometryNormal : -gs.geometryNormal);
            secondaryGBufferData.diffuseAlbedo = Pack_R11G11B10_UFLOAT(ms.diffuseAlbedo);
            secondaryGBufferData.specularAndRoughness = Pack_R8G8B8A8_Gamma_UFLOAT(float4(ms.specularF0, ms.roughness));
            secondaryGBufferData.emission = radiance;
            radiance = 0;
            secondaryGBufferData.pdf = overall_PDF;
            secondaryGBufferData.throughputAndFlags = Pack_R16G16B16A16_FLOAT(float4(1, 1, 1, 0));

            uint flags = 0;
            if (isSpecularRay) flags |= kSecondaryGBuffer_IsSpecularRay;
            if (isDeltaSurface) flags |= kSecondaryGBuffer_IsDeltaSurface;
            secondaryGBufferData.throughputAndFlags.y |= flags << 16;

            uint gbufferIndex = RTXDI_ReservoirPositionToPointer(g_Const.restirGI.reservoirBufferParams, GlobalIndex, 0);
            u_SecondaryGBuffer[gbufferIndex] = secondaryGBufferData;
        }
        else
        {
            SecondaryGBufferData secondaryGBufferData = (SecondaryGBufferData)0;
            secondaryGBufferData.worldPos = ray.Origin + ray.Direction * 10000.0;
            secondaryGBufferData.normal = ndirToOctUnorm32(-ray.Direction);
            secondaryGBufferData.throughputAndFlags = Pack_R16G16B16A16_FLOAT(float4(1, 1, 1, 0));

            uint flags = kSecondaryGBuffer_IsEnvironmentMap;
            if (isSpecularRay) flags |= kSecondaryGBuffer_IsSpecularRay;
            if (isDeltaSurface) flags |= kSecondaryGBuffer_IsDeltaSurface;
            secondaryGBufferData.throughputAndFlags.y |= flags << 16;

            uint gbufferIndex = RTXDI_ReservoirPositionToPointer(g_Const.restirGI.reservoirBufferParams, GlobalIndex, 0);
            u_SecondaryGBuffer[gbufferIndex] = secondaryGBufferData;
        }
    }
}
