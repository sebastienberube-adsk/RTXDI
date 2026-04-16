#ifndef RAB_BUFFER_HLSLI
#define RAB_BUFFER_HLSLI

#include "Rtxdi/RtxdiParameters.h"

// Previous G-buffer resources (SRV)
Texture2D<float> t_PrevGBufferDepth : register(t0);
Texture2D<uint> t_PrevGBufferNormals : register(t1);
Texture2D<uint> t_PrevGBufferGeoNormals : register(t2);
Texture2D<uint> t_PrevGBufferDiffuseAlbedo : register(t3);
Texture2D<uint> t_PrevGBufferSpecularRough : register(t4);

// Scene resources
RaytracingAccelerationStructure SceneBVH : register(t30);
StructuredBuffer<InstanceData> t_InstanceData : register(t32);
StructuredBuffer<GeometryData> t_GeometryData : register(t33);
StructuredBuffer<MaterialConstants> t_MaterialConstants : register(t34);

// RTXDI resources
StructuredBuffer<PolymorphicLightInfo> t_LightDataBuffer : register(t20);
Buffer<float2> t_NeighborOffsets : register(t21);
Buffer<uint> t_LightIndexMappingBuffer : register(t22);
Texture2D t_EnvironmentPdfTexture : register(t23);
Texture2D t_LocalLightPdfTexture : register(t24);
StructuredBuffer<uint> t_GeometryInstanceToLight : register(t25);

// Screen-sized UAVs
RWStructuredBuffer<RTXDI_PackedDIReservoir> u_LightReservoirs : register(u0);
RWTexture2D<float4> u_DiffuseLighting : register(u1);
RWTexture2D<float> u_GBufferDepth : register(u2);
RWTexture2D<uint> u_GBufferNormals : register(u3);
RWTexture2D<uint> u_GBufferGeoNormals : register(u4);
RWTexture2D<uint> u_GBufferDiffuseAlbedo : register(u5);
RWTexture2D<uint> u_GBufferSpecularRough : register(u6);
RWTexture2D<float4> u_MotionVectors : register(u7);
RWTexture2D<float4> u_Emissive : register(u8);
RWTexture2D<float4> u_SpecularLighting : register(u9);
RWTexture2D<float4> u_HdrColor : register(u10);
RWStructuredBuffer<SecondaryGBufferData> u_SecondaryGBuffer : register(u11);
RWStructuredBuffer<RTXDI_PackedGIReservoir> u_GIReservoirs : register(u12);

// RTXDI UAVs
RWBuffer<uint2> u_RisBuffer : register(u13);
RWBuffer<uint4> u_RisLightDataBuffer : register(u14);

// Other
ConstantBuffer<ResamplingConstants> g_Const : register(b0);
SamplerState s_MaterialSampler : register(s0);
SamplerState s_EnvironmentSampler : register(s1);

#define RTXDI_RIS_BUFFER u_RisBuffer
#define RTXDI_LIGHT_RESERVOIR_BUFFER u_LightReservoirs
#define RTXDI_NEIGHBOR_OFFSETS_BUFFER t_NeighborOffsets
#define RTXDI_GI_RESERVOIR_BUFFER u_GIReservoirs

#define IES_SAMPLER s_EnvironmentSampler

int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
{
    uint mappedIndexPlusOne = t_LightIndexMappingBuffer[lightIndex];
    return int(mappedIndexPlusOne) - 1;
}

#endif // RAB_BUFFER_HLSLI
