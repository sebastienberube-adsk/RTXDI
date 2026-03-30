#pragma pack_matrix(row_major)

#define RTXDI_ENABLE_PRESAMPLING 0

#include "RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    float3 diffuseAlbedo = Unpack_R11G11B10_UFLOAT(u_GBufferDiffuseAlbedo[pixelPosition]);
    float3 specularF0 = Unpack_R8G8B8A8_Gamma_UFLOAT(u_GBufferSpecularRough[pixelPosition]).rgb;
    float3 emissive = u_Emissive[pixelPosition].rgb;

    float3 diffuse = u_DiffuseLighting[pixelPosition].rgb;
    float3 specular = u_SpecularLighting[pixelPosition].rgb;

    float depth = u_GBufferDepth[pixelPosition];
    float3 color = 0;

    if (depth != BACKGROUND_DEPTH)
    {
        color = diffuse * diffuseAlbedo + specular * max(0.01, specularF0) + emissive;
    }

    if (any(isnan(color)))
        color = float3(0, 0, 1);

    if (g_Const.enableBasicToneMapping)
        color = basicToneMapping(color, 0.005);

    u_HdrColor[pixelPosition] = float4(color, 1.0);
}
