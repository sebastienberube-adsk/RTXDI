/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef SHADER_PARAMETERS_H
#define SHADER_PARAMETERS_H

#include <donut/shaders/view_cb.h>
#include <donut/shaders/sky_cb.h>
#include <Rtxdi/DI/ReSTIRDIParameters.h>
#include <Rtxdi/GI/ReSTIRGIParameters.h>

#define RTXDI_GRID_BUILD_GROUP_SIZE 256
#define RTXDI_SCREEN_SPACE_GROUP_SIZE 8

#define INSTANCE_MASK_OPAQUE 0x01
#define INSTANCE_MASK_ALPHA_TESTED 0x02
#define INSTANCE_MASK_TRANSPARENT 0x04
#define INSTANCE_MASK_ALL 0xFF

#define BACKGROUND_DEPTH 65504.f

struct PrepareLightsConstants
{
    uint numTasks;
};

struct PrepareLightsTask
{
    uint instanceIndex;
    uint geometryIndex;
    uint triangleCount;
    uint lightBufferOffset;
};

struct BRDFPathTracing_Parameters
{
    uint32_t enableReSTIRGI;
    uint32_t pad1;
    uint32_t pad2;
    uint32_t pad3;
};

struct SecondaryGBufferData
{
    float3 worldPos;
    uint normal;

    uint2 throughputAndFlags;   // .x = throughput.rg as float16, .y = throughput.b as float16, flags << 16
    uint diffuseAlbedo;         // R11G11B10_UFLOAT
    uint specularAndRoughness;  // R8G8B8A8_Gamma_UFLOAT

    float3 emission;
    float pdf;
};

static const uint32_t kSecondaryGBuffer_IsSpecularRay = 1;
static const uint32_t kSecondaryGBuffer_IsDeltaSurface = 2;
static const uint32_t kSecondaryGBuffer_IsEnvironmentMap = 4;

struct ResamplingConstants
{
    PlanarViewConstants view;
    PlanarViewConstants prevView;
    RTXDI_RuntimeParameters runtimeParams;
    RTXDI_LightBufferParameters lightBufferParams;

    ReSTIRDI_Parameters restirDI;
    ReSTIRGI_Parameters restirGI;
    BRDFPathTracing_Parameters brdfPT;

    uint frameIndex;
    uint enableResampling;
    uint enableMaterialSimilarityTest;
    uint enableBasicToneMapping;

    uint enableBrdfIndirect;
    uint pad_0;
    uint pad_1;
    uint pad_2;
};

// See TriangleLight.hlsli for encoding format
struct RAB_LightInfo
{
    // uint4[0]
    float3 center;
    uint scalars; // 2x float16
    
    // uint4[1]
    uint2 radiance; // fp16x4
    uint direction1; // oct-encoded
    uint direction2; // oct-encoded
};

#endif // SHADER_PARAMETERS_H
