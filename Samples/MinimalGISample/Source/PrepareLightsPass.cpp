/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "PrepareLightsPass.h"
#include "RtxdiResources.h"
#include "SampleScene.h"

#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>
#include <Rtxdi/DI/ReSTIRDI.h>

#include <algorithm>
#include <utility>

using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut::engine;


PrepareLightsPass::PrepareLightsPass(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory, 
    std::shared_ptr<CommonRenderPasses> commonPasses,
    std::shared_ptr<donut::engine::Scene> scene,
    nvrhi::IBindingLayout* bindlessLayout)
    : m_device(device)
    , m_bindlessLayout(bindlessLayout)
    , m_shaderFactory(std::move(shaderFactory))
    , m_commonPasses(std::move(commonPasses))
    , m_scene(std::move(scene))
{
    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::PushConstants(0, sizeof(PrepareLightsConstants)),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(1),
        nvrhi::BindingLayoutItem::Texture_UAV(2),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4),
        nvrhi::BindingLayoutItem::Sampler(0)
    };

    m_bindingLayout = m_device->createBindingLayout(bindingLayoutDesc);
}

void PrepareLightsPass::CreatePipeline()
{
    donut::log::debug("Initializing PrepareLightsPass...");

    m_computeShader = m_shaderFactory->CreateShader("app/PrepareLights.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_bindingLayout, m_bindlessLayout };
    pipelineDesc.CS = m_computeShader;
    m_computePipeline = m_device->createComputePipeline(pipelineDesc);
}

void PrepareLightsPass::CreateBindingSet(RtxdiResources& resources)
{
    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::PushConstants(0, sizeof(PrepareLightsConstants)),
        nvrhi::BindingSetItem::StructuredBuffer_UAV(0, resources.LightDataBuffer),
        nvrhi::BindingSetItem::TypedBuffer_UAV(1, resources.LightIndexMappingBuffer),
        nvrhi::BindingSetItem::Texture_UAV(2, resources.LocalLightPdfTexture),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(0, resources.TaskBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(1, resources.PrimitiveLightBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_scene->GetInstanceBuffer()),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_scene->GetGeometryBuffer()),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(4, m_scene->GetMaterialBuffer()),
        nvrhi::BindingSetItem::Sampler(0, m_commonPasses->m_AnisotropicWrapSampler)
    };

    m_bindingSet = m_device->createBindingSet(bindingSetDesc, m_bindingLayout);
    m_taskBuffer = resources.TaskBuffer;
    m_primitiveLightBuffer = resources.PrimitiveLightBuffer;
    m_lightIndexMappingBuffer = resources.LightIndexMappingBuffer;
    m_geometryInstanceToLightBuffer = resources.GeometryInstanceToLightBuffer;
    m_localLightPdfTexture = resources.LocalLightPdfTexture;
    m_maxLightsInBuffer = uint32_t(resources.LightDataBuffer->getDesc().byteSize / (sizeof(PolymorphicLightInfo) * 2));
}

void PrepareLightsPass::CountLightsInScene(uint32_t& numEmissiveMeshes, uint32_t& numEmissiveTriangles)
{
    numEmissiveMeshes = 0;
    numEmissiveTriangles = 0;

    const auto& instances = m_scene->GetSceneGraph()->GetMeshInstances();
    for (const auto& instance : instances)
    {
        for (const auto& geometry : instance->GetMesh()->geometries)
        {
            if (any(geometry->material->emissiveColor != 0.f))
            {
                numEmissiveMeshes += 1;
                numEmissiveTriangles += geometry->numIndices / 3;
            }
        }
    }
}

static inline uint floatToUInt(float _V, float _Scale)
{
    return (uint)floor(_V * _Scale + 0.5f);
}

static inline uint FLOAT3_to_R8G8B8_UNORM(float unpackedInputX, float unpackedInputY, float unpackedInputZ)
{
    return (floatToUInt(saturate(unpackedInputX), 0xFF) & 0xFF) |
        ((floatToUInt(saturate(unpackedInputY), 0xFF) & 0xFF) << 8) |
        ((floatToUInt(saturate(unpackedInputZ), 0xFF) & 0xFF) << 16);
}

static void packLightColor(const float3& color, PolymorphicLightInfo& lightInfo)
{
    float maxRadiance = std::max(color.x, std::max(color.y, color.z));

    if (maxRadiance <= 0.f)
        return;

    float logRadiance = (::log2f(maxRadiance) - kPolymorphicLightMinLog2Radiance) / (kPolymorphicLightMaxLog2Radiance - kPolymorphicLightMinLog2Radiance);
    logRadiance = saturate(logRadiance);
    uint32_t packedRadiance = std::min(uint32_t(ceilf(logRadiance * 65534.f)) + 1, 0xffffu);
    float unpackedRadiance = ::exp2f((float(packedRadiance - 1) / 65534.f) * (kPolymorphicLightMaxLog2Radiance - kPolymorphicLightMinLog2Radiance) + kPolymorphicLightMinLog2Radiance);

    lightInfo.colorTypeAndFlags |= FLOAT3_to_R8G8B8_UNORM(color.x / unpackedRadiance, color.y / unpackedRadiance, color.z / unpackedRadiance);
    lightInfo.logRadiance |= packedRadiance;
}

static float2 unitVectorToOctahedron(const float3 N)
{
    float m = abs(N.x) + abs(N.y) + abs(N.z);
    float2 XY = { N.x, N.y };
    XY.x /= m;
    XY.y /= m;
    if (N.z <= 0.0f)
    {
        float2 signs;
        signs.x = XY.x >= 0.0f ? 1.0f : -1.0f;
        signs.y = XY.y >= 0.0f ? 1.0f : -1.0f;
        float x = (1.0f - abs(XY.y)) * signs.x;
        float y = (1.0f - abs(XY.x)) * signs.y;
        XY.x = x;
        XY.y = y;
    }
    return { XY.x, XY.y };
}

static uint32_t packNormalizedVector(const float3 x)
{
    float2 XY = unitVectorToOctahedron(x);
    XY.x = XY.x * .5f + .5f;
    XY.y = XY.y * .5f + .5f;
    uint X = floatToUInt(saturate(XY.x), (1 << 16) - 1);
    uint Y = floatToUInt(saturate(XY.y), (1 << 16) - 1);
    uint packedOutput = X;
    packedOutput |= Y << 16;
    return packedOutput;
}

static uint16_t fp32ToFp16(float v)
{
    static const union FU {
        uint ui;
        float f;
    } multiple = { 0x07800000 };

    FU BiasedFloat;
    BiasedFloat.f = v * multiple.f;
    const uint u = BiasedFloat.ui;

    const uint sign = u & 0x80000000;
    uint body = u & 0x0fffffff;

    return (uint16_t)(sign >> 16 | body >> 13) & 0xFFFF;
}

static bool ConvertLight(const donut::engine::Light& light, PolymorphicLightInfo& polymorphic, bool enableImportanceSampledEnvironmentLight)
{
    switch (light.GetLightType())
    {
    case LightType_Directional: {
        auto& directional = static_cast<const donut::engine::DirectionalLight&>(light);
        float halfAngularSizeRad = 0.5f * dm::radians(directional.angularSize);
        float solidAngle = float(2 * dm::PI_d * (1.0 - cos(halfAngularSizeRad)));
        float3 radiance = directional.color * directional.irradiance / solidAngle;

        polymorphic.colorTypeAndFlags = (uint32_t)PolymorphicLightType::kDirectional << kPolymorphicLightTypeShift;
        packLightColor(radiance, polymorphic);
        polymorphic.direction1 = packNormalizedVector(float3(normalize(directional.GetDirection())));
        polymorphic.scalars = fp32ToFp16(halfAngularSizeRad) | (fp32ToFp16(solidAngle) << 16);
        return true;
    }
    case LightType_Point: {
        auto& point = static_cast<const donut::engine::PointLight&>(light);
        if (point.radius == 0.f)
        {
            float3 flux = point.color * point.intensity;

            polymorphic.colorTypeAndFlags = (uint32_t)PolymorphicLightType::kPoint << kPolymorphicLightTypeShift;
            packLightColor(flux, polymorphic);
            polymorphic.center = float3(point.GetPosition());
        }
        else
        {
            float projectedArea = dm::PI_f * square(point.radius);
            float3 radiance = point.color * point.intensity / projectedArea;

            polymorphic.colorTypeAndFlags = (uint32_t)PolymorphicLightType::kSphere << kPolymorphicLightTypeShift;
            packLightColor(radiance, polymorphic);
            polymorphic.center = float3(point.GetPosition());
            polymorphic.scalars = fp32ToFp16(point.radius);
        }

        return true;
    }
    case LightType_Environment: {
        auto& env = static_cast<const EnvironmentLight&>(light);

        if (env.textureIndex < 0)
            return false;
        
        polymorphic.colorTypeAndFlags = (uint32_t)PolymorphicLightType::kEnvironment << kPolymorphicLightTypeShift;
        packLightColor(env.radianceScale, polymorphic);
        polymorphic.direction1 = (uint32_t)env.textureIndex;
        polymorphic.direction2 = env.textureSize.x | (env.textureSize.y << 16);
        polymorphic.scalars = fp32ToFp16(env.rotation);
        if (enableImportanceSampledEnvironmentLight)
            polymorphic.scalars |= (1 << 16);

        return true;
    }
    default:
        return false;
    }
}

static int isInfiniteLight(const donut::engine::Light& light)
{
    switch (light.GetLightType())
    {
    case LightType_Directional:
        return 1;

    case LightType_Environment:
        return 2;

    default:
        return 0;
    }
}

RTXDI_LightBufferParameters PrepareLightsPass::Process(
    nvrhi::ICommandList* commandList, 
    const rtxdi::ReSTIRDIContext& context,
    const std::vector<std::shared_ptr<donut::engine::Light>>& sceneLights,
    bool enableImportanceSampledEnvironmentLight)
{
    RTXDI_LightBufferParameters outLightBufferParams = {};

    commandList->beginMarker("PrepareLights");

    std::vector<PrepareLightsTask> tasks;
    std::vector<PolymorphicLightInfo> primitiveLightInfos;
    uint32_t lightBufferOffset = 0;
    std::vector<uint32_t> geometryInstanceToLight(m_scene->GetSceneGraph()->GetGeometryInstancesCount(), RTXDI_INVALID_LIGHT_INDEX);

    const auto& instances = m_scene->GetSceneGraph()->GetMeshInstances();
    for (const auto& instance : instances)
    {
        const auto& mesh = instance->GetMesh();

        assert(instance->GetGeometryInstanceIndex() < geometryInstanceToLight.size());
        uint32_t firstGeometryInstanceIndex = instance->GetGeometryInstanceIndex();

        for (size_t geometryIndex = 0; geometryIndex < mesh->geometries.size(); ++geometryIndex)
        {
            const auto& geometry = mesh->geometries[geometryIndex];

            size_t instanceHash = 0;
            nvrhi::hash_combine(instanceHash, instance.get());
            nvrhi::hash_combine(instanceHash, geometryIndex);

            if (!any(geometry->material->emissiveColor != 0.f) || geometry->material->emissiveIntensity <= 0.f)
            {
                m_instanceLightBufferOffsets.erase(instanceHash);
                continue;
            }

            geometryInstanceToLight[firstGeometryInstanceIndex + geometryIndex] = lightBufferOffset;

            auto pOffset = m_instanceLightBufferOffsets.find(instanceHash);

            assert(geometryIndex < 0xfff);

            PrepareLightsTask task;
            task.instanceAndGeometryIndex = (instance->GetInstanceIndex() << 12) | uint32_t(geometryIndex & 0xfff);
            task.lightBufferOffset = lightBufferOffset;
            task.triangleCount = geometry->numIndices / 3;
            task.previousLightBufferOffset = (pOffset != m_instanceLightBufferOffsets.end()) ? int(pOffset->second) : -1;

            m_instanceLightBufferOffsets[instanceHash] = lightBufferOffset;

            lightBufferOffset += task.triangleCount;

            tasks.push_back(task);
        }
    }

    commandList->writeBuffer(m_geometryInstanceToLightBuffer, geometryInstanceToLight.data(), geometryInstanceToLight.size() * sizeof(uint32_t));

    outLightBufferParams.localLightBufferRegion.firstLightIndex = 0;
    outLightBufferParams.localLightBufferRegion.numLights = lightBufferOffset;

    auto sortedLights = sceneLights;
    std::sort(sortedLights.begin(), sortedLights.end(), [](const auto& a, const auto& b) 
        { return isInfiniteLight(*a) < isInfiniteLight(*b); });

    uint32_t numFinitePrimLights = 0;
    uint32_t numInfinitePrimLights = 0;
    uint32_t numImportanceSampledEnvironmentLights = 0;

    for (const std::shared_ptr<Light>& pLight : sortedLights)
    {
        PolymorphicLightInfo polymorphicLight = {};

        if (!ConvertLight(*pLight, polymorphicLight, enableImportanceSampledEnvironmentLight))
            continue;

        auto pOffset = m_primitiveLightBufferOffsets.find(pLight.get());

        PrepareLightsTask task;
        task.instanceAndGeometryIndex = TASK_PRIMITIVE_LIGHT_BIT | uint32_t(primitiveLightInfos.size());
        task.lightBufferOffset = lightBufferOffset;
        task.triangleCount = 1;
        task.previousLightBufferOffset = (pOffset != m_primitiveLightBufferOffsets.end()) ? pOffset->second : -1;

        m_primitiveLightBufferOffsets[pLight.get()] = lightBufferOffset;

        lightBufferOffset += task.triangleCount;

        tasks.push_back(task);
        primitiveLightInfos.push_back(polymorphicLight);

        if (pLight->GetLightType() == LightType_Environment && enableImportanceSampledEnvironmentLight)
            numImportanceSampledEnvironmentLights++;
        else if (isInfiniteLight(*pLight))
            numInfinitePrimLights++;
        else
            numFinitePrimLights++;
    }

    assert(numImportanceSampledEnvironmentLights <= 1);
    
    outLightBufferParams.localLightBufferRegion.numLights += numFinitePrimLights;
    outLightBufferParams.infiniteLightBufferRegion.firstLightIndex = outLightBufferParams.localLightBufferRegion.numLights;
    outLightBufferParams.infiniteLightBufferRegion.numLights = numInfinitePrimLights;
    outLightBufferParams.environmentLightParams.lightIndex = outLightBufferParams.infiniteLightBufferRegion.firstLightIndex + outLightBufferParams.infiniteLightBufferRegion.numLights;
    outLightBufferParams.environmentLightParams.lightPresent = numImportanceSampledEnvironmentLights;
    
    commandList->writeBuffer(m_taskBuffer, tasks.data(), tasks.size() * sizeof(PrepareLightsTask));

    if (!primitiveLightInfos.empty())
    {
        commandList->writeBuffer(m_primitiveLightBuffer, primitiveLightInfos.data(), primitiveLightInfos.size() * sizeof(PolymorphicLightInfo));
    }

    commandList->clearBufferUInt(m_lightIndexMappingBuffer, 0);

    commandList->clearTextureFloat(m_localLightPdfTexture, 
        nvrhi::TextureSubresourceSet(0, 1, 0, 1), 
        nvrhi::Color(0.f));

    nvrhi::ComputeState state;
    state.pipeline = m_computePipeline;
    state.bindings = { m_bindingSet, m_scene->GetDescriptorTable() };
    commandList->setComputeState(state);

    PrepareLightsConstants constants;
    constants.numTasks = uint32_t(tasks.size());
    constants.currentFrameLightOffset = m_maxLightsInBuffer * m_oddFrame;
    constants.previousFrameLightOffset = m_maxLightsInBuffer * !m_oddFrame;
    commandList->setPushConstants(&constants, sizeof(constants));

    commandList->dispatch(dm::div_ceil(lightBufferOffset, 256));

    commandList->endMarker();

    outLightBufferParams.localLightBufferRegion.firstLightIndex += constants.currentFrameLightOffset;
    outLightBufferParams.infiniteLightBufferRegion.firstLightIndex += constants.currentFrameLightOffset;
    outLightBufferParams.environmentLightParams.lightIndex += constants.currentFrameLightOffset;

    m_oddFrame = !m_oddFrame;
    return outLightBufferParams;
}
