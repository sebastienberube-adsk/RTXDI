/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <memory>
#include <Rtxdi/DI/ReSTIRDI.h>

namespace donut::engine
{
    class Scene;
    class CommonRenderPasses;
    class IView;
    class ShaderFactory;
    struct ShaderMacro;
}

namespace rtxdi
{
    class ReSTIRDIContext;
}

class RenderTargets;
class RtxdiResources;
class EnvironmentLight;
struct ResamplingConstants;

class LightingPasses
{
public:
    struct Settings
    {
        rtxdi::ReSTIRDI_ResamplingMode resamplingMode = rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial;
        bool unbiasedMode = false;
        bool discardInvisibleSamples = true;
        bool enableMaterialSimilarityTest = true;

        uint32_t numInitialSamples = 8;
        uint32_t numSpatialSamples = 1;
        uint32_t numDisocclusionBoostSamples = 0;
        uint32_t numInitialBRDFSamples = 1;
        float brdfCutoff = 0.f;
        bool enableBasicToneMapping = true;
    };

    LightingPasses(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
        std::shared_ptr<donut::engine::Scene> scene,
        nvrhi::IBindingLayout* bindlessLayout);

    void CreatePipeline();

    void CreateBindingSet(
        nvrhi::rt::IAccelStruct* topLevelAS,
        const RenderTargets& renderTargets,
        const RtxdiResources& resources);

    void Render(
        nvrhi::ICommandList* commandList,
        rtxdi::ReSTIRDIContext& context,
        const donut::engine::IView& view,
        const donut::engine::IView& previousView,
        const Settings& localSettings,
        const RTXDI_LightBufferParameters& lightBufferParams);

    void NextFrame();

private:
    nvrhi::DeviceHandle m_device;

    nvrhi::ShaderHandle m_gbufferShader;
    nvrhi::ShaderHandle m_initialSamplingShader;
    nvrhi::ShaderHandle m_temporalResamplingShader;
    nvrhi::ShaderHandle m_spatialResamplingShader;
    nvrhi::ShaderHandle m_shadeSamplesShader;
    nvrhi::ShaderHandle m_DIFusedResamplingShader;
    nvrhi::ShaderHandle m_compositingShader;

    nvrhi::ComputePipelineHandle m_gbufferPipeline;
    nvrhi::ComputePipelineHandle m_initialSamplingPipeline;
    nvrhi::ComputePipelineHandle m_temporalResamplingPipeline;
    nvrhi::ComputePipelineHandle m_spatialResamplingPipeline;
    nvrhi::ComputePipelineHandle m_shadeSamplesPipeline;
    nvrhi::ComputePipelineHandle m_DIFusedResamplingPipeline;
    nvrhi::ComputePipelineHandle m_compositingPipeline;

    nvrhi::BindingLayoutHandle m_bindingLayout;
    nvrhi::BindingLayoutHandle m_bindlessLayout;
    nvrhi::BindingSetHandle m_bindingSet;
    nvrhi::BindingSetHandle m_prevBindingSet;
    nvrhi::BufferHandle m_constantBuffer;
    nvrhi::BufferHandle m_lightReservoirBuffer;
    nvrhi::TextureHandle m_diffuseLightingTexture;
    nvrhi::TextureHandle m_specularLightingTexture;

    std::shared_ptr<donut::engine::ShaderFactory> m_shaderFactory;
    std::shared_ptr<donut::engine::CommonRenderPasses> m_commonPasses;
    std::shared_ptr<donut::engine::Scene> m_scene;
};
