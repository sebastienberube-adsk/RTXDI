#pragma pack_matrix(row_major)

#define RTXDI_ENABLE_PRESAMPLING 0

#include "RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include <Rtxdi/DI/Reservoir.hlsli>

#include "ShadingHelpers.hlsli"

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

        bool needToStore = ShadeSurfaceWithLightSample(reservoir, surface, lightSample,
            /* enableVisibilityReuse = */ true, diffuse, specular);

        specular = DemodulateSpecular(surface.material.specularF0, specular);

        if (needToStore)
        {
            RTXDI_StoreDIReservoir(reservoir, g_Const.restirDI.reservoirBufferParams,
                pixelPosition, g_Const.restirDI.bufferIndices.shadingInputBufferIndex);
        }
    }

    StoreShadingOutput(pixelPosition, diffuse, specular, true);
}
