#ifndef RAB_LIGHT_INFO_HLSLI
#define RAB_LIGHT_INFO_HLSLI

#include "../PolymorphicLight.hlsli"
#include "RAB_Surface.hlsli"
#include "RAB_LightSample.hlsli"

typedef PolymorphicLightInfo RAB_LightInfo;

RAB_LightInfo RAB_EmptyLightInfo()
{
    return (RAB_LightInfo)0;
}

RAB_LightInfo RAB_LoadLightInfo(uint index, bool previousFrame)
{
    return t_LightDataBuffer[index];
}

RAB_LightInfo RAB_LoadCompactLightInfo(uint linearIndex)
{
    uint4 packedData1, packedData2;
    packedData1 = u_RisLightDataBuffer[linearIndex * 2 + 0];
    packedData2 = u_RisLightDataBuffer[linearIndex * 2 + 1];
    return unpackCompactLightInfo(packedData1, packedData2);
}

bool RAB_StoreCompactLightInfo(uint linearIndex, RAB_LightInfo lightInfo)
{
    uint4 data1, data2;
    if (!packCompactLightInfo(lightInfo, data1, data2))
        return false;

    u_RisLightDataBuffer[linearIndex * 2 + 0] = data1;
    u_RisLightDataBuffer[linearIndex * 2 + 1] = data2;

    return true;
}

float RAB_GetLightTargetPdfForVolume(RAB_LightInfo light, float3 volumeCenter, float volumeRadius)
{
    return PolymorphicLight::getWeightForVolume(light, volumeCenter, volumeRadius);
}

RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    PolymorphicLightSample pls = PolymorphicLight::calcSample(lightInfo, uv, surface.worldPos);

    RAB_LightSample lightSample;
    lightSample.position = pls.position;
    lightSample.normal = pls.normal;
    lightSample.radiance = pls.radiance;
    lightSample.solidAnglePdf = pls.solidAnglePdf;
    lightSample.lightType = getLightType(lightInfo);
    return lightSample;
}

#endif // RAB_LIGHT_INFO_HLSLI
