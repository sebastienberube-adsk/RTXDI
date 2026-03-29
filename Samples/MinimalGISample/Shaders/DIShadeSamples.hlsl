#pragma pack_matrix(row_major)

#define RTXDI_ENABLE_PRESAMPLING 0

#include "RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"
#include "ShadingHelpers.hlsli"

#include <Rtxdi/DI/Reservoir.hlsli>

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(g_Const.restirDI.reservoirBufferParams,
        pixelPosition, g_Const.restirDI.bufferIndices.shadingInputBufferIndex);

    float3 diffuse = 0;
    float3 specular = 0;

    if (RAB_IsSurfaceValid(surface) && RTXDI_IsValidDIReservoir(reservoir))
    {
        RAB_LightInfo lightInfo = RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(reservoir), false);
        RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo,
            surface, RTXDI_GetDIReservoirSampleUV(reservoir));

        if (lightSample.solidAnglePdf > 0)
        {
            float3 L = normalize(lightSample.position - surface.worldPos);
            if (dot(L, surface.geoNormal) > 0)
            {
                SplitBrdf brdf = EvaluateBrdf(surface, lightSample.position);

                float3 radiance = lightSample.radiance * RTXDI_GetDIReservoirInvPdf(reservoir)
                                / lightSample.solidAnglePdf;

                bool visibility = RAB_GetConservativeVisibility(surface, lightSample);
                if (!visibility)
                {
                    radiance = 0;
                    RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
                    RTXDI_StoreDIReservoir(reservoir, g_Const.restirDI.reservoirBufferParams,
                        pixelPosition, g_Const.restirDI.bufferIndices.shadingInputBufferIndex);
                }

                diffuse = brdf.demodulatedDiffuse * radiance;
                specular = brdf.specular * radiance;
            }
        }
    }

    specular = DemodulateSpecular(surface.material.specularF0, specular);

    StoreShadingOutput(pixelPosition, diffuse, specular, true);
}
