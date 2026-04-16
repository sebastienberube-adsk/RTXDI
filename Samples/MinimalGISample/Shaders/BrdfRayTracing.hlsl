/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#include "RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include <Rtxdi/DI/Reservoir.hlsli>

#include "ShadingHelpers.hlsli"

static const float c_MaxIndirectRadiance = 10;

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    if (!RAB_IsSurfaceValid(surface))
        return;

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 5);

    float3 tangent, bitangent;
    branchlessONB(surface.normal, tangent, bitangent);

    float distance = max(1, 0.1 * length(surface.worldPos - g_Const.view.cameraDirectionOrPosition.xyz));

    RayDesc ray;
    ray.TMin = 0.001f * distance;
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
            float3 He = sampleGGX_VNDF(Ve, surface.material.roughness, Rand);
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
    float throughput = 1.0;

    struct
    {
        float3 position;
        float3 normal;
        float3 diffuseAlbedo;
        float3 specularF0;
        float roughness;
    } secondarySurface;

    RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_OPAQUE, ray);
    rayQuery.Proceed();

    bool enableReSTIRGI = g_Const.brdfPT.enableReSTIRGI;
    bool includeEmissiveComponent = !enableReSTIRGI || (isSpecularRay && isDeltaSurface);

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
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

        if (includeEmissiveComponent)
            radiance += ms.emissiveColor;

        secondarySurface.position = ray.Origin + ray.Direction * committedRayT;
        secondarySurface.normal = (dot(gs.geometryNormal, ray.Direction) < 0) ? gs.geometryNormal : -gs.geometryNormal;
        secondarySurface.diffuseAlbedo = ms.diffuseAlbedo;
        secondarySurface.specularF0 = ms.specularF0;
        secondarySurface.roughness = ms.roughness;
    }
    else
    {
        if (g_Const.sceneConstants.enableEnvironmentMap && includeEmissiveComponent)
        {
            float3 environmentRadiance = GetEnvironmentRadiance(ray.Direction);
            radiance += environmentRadiance;
        }

        secondarySurface.position = ray.Origin + ray.Direction * DISTANT_LIGHT_DISTANCE;
        secondarySurface.normal = -ray.Direction;
        secondarySurface.diffuseAlbedo = 0;
        secondarySurface.specularF0 = 0;
        secondarySurface.roughness = 0;
    }

    if (g_Const.enableBrdfIndirect)
    {
        uint gbufferIndex = RTXDI_ReservoirPositionToPointer(g_Const.restirGI.reservoirBufferParams, pixelPosition, 0);

        SecondaryGBufferData secondaryGBufferData = (SecondaryGBufferData)0;
        secondaryGBufferData.worldPos = secondarySurface.position;
        secondaryGBufferData.normal = ndirToOctUnorm32(secondarySurface.normal);
        secondaryGBufferData.throughputAndFlags = Pack_R16G16B16A16_FLOAT(float4(throughput * BRDF_over_PDF, 0));
        secondaryGBufferData.diffuseAlbedo = Pack_R11G11B10_UFLOAT(secondarySurface.diffuseAlbedo);
        secondaryGBufferData.specularAndRoughness = Pack_R8G8B8A8_Gamma_UFLOAT(float4(secondarySurface.specularF0, secondarySurface.roughness));

        if (enableReSTIRGI)
        {
            if (!(isSpecularRay && isDeltaSurface))
            {
                secondaryGBufferData.throughputAndFlags = Pack_R16G16B16A16_FLOAT(float4(throughput, 0, 0, 0));
            }

            secondaryGBufferData.emission = radiance;
            radiance = 0;
            secondaryGBufferData.pdf = overall_PDF;
        }

        uint flags = 0;
        if (isSpecularRay) flags |= kSecondaryGBuffer_IsSpecularRay;
        if (isDeltaSurface) flags |= kSecondaryGBuffer_IsDeltaSurface;
        if (rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT) flags |= kSecondaryGBuffer_IsEnvironmentMap;
        secondaryGBufferData.throughputAndFlags.y |= flags << 16;

        u_SecondaryGBuffer[gbufferIndex] = secondaryGBufferData;
    }

    if (any(radiance > 0) || !g_Const.enableBrdfIndirect)
    {
        radiance *= throughput;

        float3 diffuse = isSpecularRay ? 0.0 : radiance * BRDF_over_PDF;
        float3 specular = isSpecularRay ? radiance * BRDF_over_PDF : 0.0;

        specular = DemodulateSpecular(surface.material.specularF0, specular);

        StoreShadingOutput(pixelPosition, diffuse, specular, false);
    }
}
