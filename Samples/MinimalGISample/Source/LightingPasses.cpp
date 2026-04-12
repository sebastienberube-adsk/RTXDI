/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "LightingPasses.h"
#include "RenderTargets.h"
#include "RtxdiResources.h"

#include <donut/engine/Scene.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>
#include <Rtxdi/DI/ReSTIRDI.h>

using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut::engine;

LightingPasses::LightingPasses(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
    std::shared_ptr<donut::engine::Scene> scene,
    nvrhi::IBindingLayout* bindlessLayout
)
    : m_device(device)
    , m_bindlessLayout(bindlessLayout)
    , m_shaderFactory(std::move(shaderFactory))
    , m_commonPasses(std::move(commonPasses))
    , m_scene(std::move(scene))
{
    nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
    globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute | nvrhi::ShaderType::AllRayTracing;
    globalBindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1),
        nvrhi::BindingLayoutItem::Texture_SRV(2),
        nvrhi::BindingLayoutItem::Texture_SRV(3),
        nvrhi::BindingLayoutItem::Texture_SRV(4),

        nvrhi::BindingLayoutItem::RayTracingAccelStruct(30),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(32),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(33),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(34),

        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(20),
        nvrhi::BindingLayoutItem::TypedBuffer_SRV(21),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(22),

        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(1),
        nvrhi::BindingLayoutItem::Texture_UAV(2),
        nvrhi::BindingLayoutItem::Texture_UAV(3),
        nvrhi::BindingLayoutItem::Texture_UAV(4),
        nvrhi::BindingLayoutItem::Texture_UAV(5),
        nvrhi::BindingLayoutItem::Texture_UAV(6),
        nvrhi::BindingLayoutItem::Texture_UAV(7),
        nvrhi::BindingLayoutItem::Texture_UAV(8),
        nvrhi::BindingLayoutItem::Texture_UAV(9),
        nvrhi::BindingLayoutItem::Texture_UAV(10),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(11),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(12),

        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Sampler(1),
    };

    m_bindingLayout = m_device->createBindingLayout(globalBindingLayoutDesc);

    m_constantBuffer = m_device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(ResamplingConstants), "ResamplingConstants", 16));
}

void LightingPasses::CreateBindingSet(
    nvrhi::rt::IAccelStruct* topLevelAS,
    const RenderTargets& renderTargets,
    const RtxdiResources& resources)
{
    assert(&renderTargets);
    assert(&resources);

    for (int currentFrame = 0; currentFrame <= 1; currentFrame++)
    {
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::Texture_SRV(0, currentFrame ? renderTargets.PrevDepth : renderTargets.Depth),
            nvrhi::BindingSetItem::Texture_SRV(1, currentFrame ? renderTargets.PrevGBufferNormals : renderTargets.GBufferNormals),
            nvrhi::BindingSetItem::Texture_SRV(2, currentFrame ? renderTargets.PrevGBufferGeoNormals : renderTargets.GBufferGeoNormals),
            nvrhi::BindingSetItem::Texture_SRV(3, currentFrame ? renderTargets.PrevGBufferDiffuseAlbedo : renderTargets.GBufferDiffuseAlbedo),
            nvrhi::BindingSetItem::Texture_SRV(4, currentFrame ? renderTargets.PrevGBufferSpecularRough : renderTargets.GBufferSpecularRough),
            
            nvrhi::BindingSetItem::RayTracingAccelStruct(30, topLevelAS),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(32, m_scene->GetInstanceBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(33, m_scene->GetGeometryBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(34, m_scene->GetMaterialBuffer()),

            nvrhi::BindingSetItem::StructuredBuffer_SRV(20, resources.LightDataBuffer),
            nvrhi::BindingSetItem::TypedBuffer_SRV(21, resources.NeighborOffsetsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(22, resources.GeometryInstanceToLightBuffer),

            nvrhi::BindingSetItem::StructuredBuffer_UAV(0, resources.LightReservoirBuffer),
            nvrhi::BindingSetItem::Texture_UAV(1, renderTargets.DiffuseLighting),
            nvrhi::BindingSetItem::Texture_UAV(2, currentFrame ? renderTargets.Depth : renderTargets.PrevDepth),
            nvrhi::BindingSetItem::Texture_UAV(3, currentFrame ? renderTargets.GBufferNormals : renderTargets.PrevGBufferNormals),
            nvrhi::BindingSetItem::Texture_UAV(4, currentFrame ? renderTargets.GBufferGeoNormals : renderTargets.PrevGBufferGeoNormals),
            nvrhi::BindingSetItem::Texture_UAV(5, currentFrame ? renderTargets.GBufferDiffuseAlbedo : renderTargets.PrevGBufferDiffuseAlbedo),
            nvrhi::BindingSetItem::Texture_UAV(6, currentFrame ? renderTargets.GBufferSpecularRough : renderTargets.PrevGBufferSpecularRough),
            nvrhi::BindingSetItem::Texture_UAV(7, renderTargets.MotionVectors),
            nvrhi::BindingSetItem::Texture_UAV(8, renderTargets.GBufferEmissive),
            nvrhi::BindingSetItem::Texture_UAV(9, renderTargets.SpecularLighting),
            nvrhi::BindingSetItem::Texture_UAV(10, renderTargets.HdrColor),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(11, resources.SecondaryGBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(12, resources.GIReservoirBuffer),
            
            nvrhi::BindingSetItem::ConstantBuffer(0, m_constantBuffer),
            nvrhi::BindingSetItem::Sampler(0, m_commonPasses->m_AnisotropicWrapSampler),
            nvrhi::BindingSetItem::Sampler(1, m_commonPasses->m_LinearWrapSampler)
        };

        const nvrhi::BindingSetHandle bindingSet = m_device->createBindingSet(bindingSetDesc, m_bindingLayout);

        if (currentFrame)
            m_bindingSet = bindingSet;
        else
            m_prevBindingSet = bindingSet;
    }
    
    m_lightReservoirBuffer = resources.LightReservoirBuffer;
    m_secondaryGBuffer = resources.SecondaryGBuffer;
    m_giReservoirBuffer = resources.GIReservoirBuffer;
    m_diffuseLightingTexture = renderTargets.DiffuseLighting;
    m_specularLightingTexture = renderTargets.SpecularLighting;
}

void LightingPasses::CreatePipeline()
{
    m_gbufferShader = m_shaderFactory->CreateShader("app/GBufferPass.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_initialSamplingShader = m_shaderFactory->CreateShader("app/DIGenerateInitialSamples.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_temporalResamplingShader = m_shaderFactory->CreateShader("app/DITemporalResampling.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_spatialResamplingShader = m_shaderFactory->CreateShader("app/DISpatialResampling.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_shadeSamplesShader = m_shaderFactory->CreateShader("app/DIShadeSamples.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_DIFusedResamplingShader = m_shaderFactory->CreateShader("app/DIFusedResampling.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_brdfRayTracingShader = m_shaderFactory->CreateShader("app/BrdfRayTracing.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_shadeSecondarySurfacesShader = m_shaderFactory->CreateShader("app/ShadeSecondarySurfaces.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_giTemporalResamplingShader = m_shaderFactory->CreateShader("app/GITemporalResampling.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_giSpatialResamplingShader = m_shaderFactory->CreateShader("app/GISpatialResampling.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_giFusedResamplingShader = m_shaderFactory->CreateShader("app/GIFusedResampling.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_giFinalShadingShader = m_shaderFactory->CreateShader("app/GIFinalShading.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_compositingShader = m_shaderFactory->CreateShader("app/Compositing.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_bindingLayout, m_bindlessLayout };

    pipelineDesc.CS = m_gbufferShader;
    m_gbufferPipeline = m_device->createComputePipeline(pipelineDesc);

    pipelineDesc.CS = m_initialSamplingShader;
    m_initialSamplingPipeline = m_device->createComputePipeline(pipelineDesc);

    pipelineDesc.CS = m_temporalResamplingShader;
    m_temporalResamplingPipeline = m_device->createComputePipeline(pipelineDesc);

    pipelineDesc.CS = m_spatialResamplingShader;
    m_spatialResamplingPipeline = m_device->createComputePipeline(pipelineDesc);

    pipelineDesc.CS = m_shadeSamplesShader;
    m_shadeSamplesPipeline = m_device->createComputePipeline(pipelineDesc);

    pipelineDesc.CS = m_DIFusedResamplingShader;
    m_DIFusedResamplingPipeline = m_device->createComputePipeline(pipelineDesc);

    pipelineDesc.CS = m_brdfRayTracingShader;
    m_brdfRayTracingPipeline = m_device->createComputePipeline(pipelineDesc);

    pipelineDesc.CS = m_shadeSecondarySurfacesShader;
    m_shadeSecondarySurfacesPipeline = m_device->createComputePipeline(pipelineDesc);

    pipelineDesc.CS = m_giTemporalResamplingShader;
    m_giTemporalResamplingPipeline = m_device->createComputePipeline(pipelineDesc);

    pipelineDesc.CS = m_giSpatialResamplingShader;
    m_giSpatialResamplingPipeline = m_device->createComputePipeline(pipelineDesc);

    pipelineDesc.CS = m_giFusedResamplingShader;
    m_giFusedResamplingPipeline = m_device->createComputePipeline(pipelineDesc);

    pipelineDesc.CS = m_giFinalShadingShader;
    m_giFinalShadingPipeline = m_device->createComputePipeline(pipelineDesc);

    pipelineDesc.CS = m_compositingShader;
    m_compositingPipeline = m_device->createComputePipeline(pipelineDesc);
}

void LightingPasses::Render(
    nvrhi::ICommandList* commandList,
    rtxdi::ReSTIRDIContext& context,
    rtxdi::ReSTIRGIContext& giContext,
    const donut::engine::IView& view,
    const donut::engine::IView& previousView,
    const Settings& localSettings,
    const RTXDI_LightBufferParameters& lightBufferParams)
{
    context.SetResamplingMode(localSettings.resamplingMode);

    auto initialSamplingParams = context.GetInitialSamplingParameters();
    initialSamplingParams.numPrimaryLocalLightSamples = localSettings.numInitialSamples;
    initialSamplingParams.numPrimaryBrdfSamples = localSettings.numInitialBRDFSamples;
    initialSamplingParams.brdfCutoff = localSettings.brdfCutoff;
    context.SetInitialSamplingParameters(initialSamplingParams);

    auto temporalParams = context.GetTemporalResamplingParameters();
    temporalParams.temporalBiasCorrection = localSettings.unbiasedMode
        ? ReSTIRDI_TemporalBiasCorrectionMode::Raytraced
        : ReSTIRDI_TemporalBiasCorrectionMode::Basic;
    temporalParams.discardInvisibleSamples = localSettings.discardInvisibleSamples;
    context.SetTemporalResamplingParameters(temporalParams);

    auto spatialParams = context.GetSpatialResamplingParameters();
    spatialParams.numSpatialSamples = localSettings.numSpatialSamples;
    spatialParams.numDisocclusionBoostSamples = localSettings.numDisocclusionBoostSamples;
    context.SetSpatialResamplingParameters(spatialParams);

    ResamplingConstants constants = {};
    constants.frameIndex = context.GetFrameIndex();
    view.FillPlanarViewConstants(constants.view);
    previousView.FillPlanarViewConstants(constants.prevView);

    constants.enableResampling = (localSettings.resamplingMode != rtxdi::ReSTIRDI_ResamplingMode::None);
    constants.enableMaterialSimilarityTest = localSettings.enableMaterialSimilarityTest;
    constants.enableBasicToneMapping = localSettings.enableBasicToneMapping;
    constants.basicTonemapBias = localSettings.basicTonemapBias;
    constants.lightBufferParams = lightBufferParams;
    constants.runtimeParams = context.GetRuntimeParams();

    constants.restirDI.reservoirBufferParams = context.GetReservoirBufferParameters();
    constants.restirDI.bufferIndices = context.GetBufferIndices();
    constants.restirDI.initialSamplingParams = context.GetInitialSamplingParameters();
    constants.restirDI.temporalResamplingParams = context.GetTemporalResamplingParameters();
    constants.restirDI.spatialResamplingParams = context.GetSpatialResamplingParameters();
    constants.restirDI.shadingParams = context.GetShadingParameters();

    giContext.SetResamplingMode(localSettings.giResamplingMode);
    constants.restirGI.reservoirBufferParams = giContext.GetReservoirBufferParameters();
    constants.restirGI.bufferIndices = giContext.GetBufferIndices();
    constants.restirGI.temporalResamplingParams = giContext.GetTemporalResamplingParameters();
    constants.restirGI.spatialResamplingParams = giContext.GetSpatialResamplingParameters();
    constants.restirGI.finalShadingParams = giContext.GetFinalShadingParameters();

    constants.enableBrdfIndirect = localSettings.enableBrdfIndirect ? 1 : 0;
    constants.brdfPT.enableReSTIRGI = localSettings.enableBrdfIndirect ? 1 : 0;
    constants.diagMode = localSettings.diagMode;

    commandList->writeBuffer(m_constantBuffer, &constants, sizeof(constants));

    uint32_t dispatchWidth = dm::div_ceil(view.GetViewExtent().width(), RTXDI_SCREEN_SPACE_GROUP_SIZE);
    uint32_t dispatchHeight = dm::div_ceil(view.GetViewExtent().height(), RTXDI_SCREEN_SPACE_GROUP_SIZE);

    nvrhi::ComputeState state;
    state.bindings = { m_bindingSet, m_scene->GetDescriptorTable() };

    commandList->beginMarker("GBufferPass");
    state.pipeline = m_gbufferPipeline;
    commandList->setComputeState(state);
    commandList->dispatch(dispatchWidth, dispatchHeight);
    commandList->endMarker();

    // Run the lighting passes in the necessary sequence: one fused kernel or multiple separate passes.
    //
    // Note: the below code places explicit UAV barriers between subsequent passes
    // because NVRHI misses them, as the binding sets are exactly the same between these passes.
    // That equality makes NVRHI take a shortcut for performance and it doesn't look at bindings at all.

    const auto mode = localSettings.resamplingMode;

    if (mode == rtxdi::ReSTIRDI_ResamplingMode::FusedSpatiotemporal)
    {
        nvrhi::utils::BufferUavBarrier(commandList, m_lightReservoirBuffer);

        commandList->beginMarker("DIFusedResampling");
        state.pipeline = m_DIFusedResamplingPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight);
        commandList->endMarker();
    }
    else
    {
        commandList->beginMarker("DIGenerateInitialSamples");
        state.pipeline = m_initialSamplingPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight);
        commandList->endMarker();

        if (mode == rtxdi::ReSTIRDI_ResamplingMode::Temporal ||
            mode == rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial)
        {
            nvrhi::utils::BufferUavBarrier(commandList, m_lightReservoirBuffer);

            commandList->beginMarker("DITemporalResampling");
            state.pipeline = m_temporalResamplingPipeline;
            commandList->setComputeState(state);
            commandList->dispatch(dispatchWidth, dispatchHeight);
            commandList->endMarker();
        }

        if (mode == rtxdi::ReSTIRDI_ResamplingMode::Spatial ||
            mode == rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial)
        {
            nvrhi::utils::BufferUavBarrier(commandList, m_lightReservoirBuffer);

            commandList->beginMarker("DISpatialResampling");
            state.pipeline = m_spatialResamplingPipeline;
            commandList->setComputeState(state);
            commandList->dispatch(dispatchWidth, dispatchHeight);
            commandList->endMarker();
        }

        nvrhi::utils::BufferUavBarrier(commandList, m_lightReservoirBuffer);

        commandList->beginMarker("DIShadeSamples");
        state.pipeline = m_shadeSamplesPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight);
        commandList->endMarker();
    }

    bool enableBrdfIndirect = localSettings.enableBrdfIndirect;
    if (enableBrdfIndirect)
    {
        nvrhi::utils::TextureUavBarrier(commandList, m_diffuseLightingTexture);
        nvrhi::utils::TextureUavBarrier(commandList, m_specularLightingTexture);

        commandList->beginMarker("BrdfRayTracing");
        state.pipeline = m_brdfRayTracingPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight);
        commandList->endMarker();

        nvrhi::utils::BufferUavBarrier(commandList, m_secondaryGBuffer);

        commandList->beginMarker("ShadeSecondarySurfaces");
        state.pipeline = m_shadeSecondarySurfacesPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight);
        commandList->endMarker();

        nvrhi::utils::BufferUavBarrier(commandList, m_giReservoirBuffer);

        const rtxdi::ReSTIRGI_ResamplingMode giMode = localSettings.giResamplingMode;
        if (giMode == rtxdi::ReSTIRGI_ResamplingMode::FusedSpatiotemporal)
        {
            commandList->beginMarker("GIFusedResampling");
            state.pipeline = m_giFusedResamplingPipeline;
            commandList->setComputeState(state);
            commandList->dispatch(dispatchWidth, dispatchHeight);
            commandList->endMarker();
        }
        else
        {
            if (giMode == rtxdi::ReSTIRGI_ResamplingMode::Temporal ||
                giMode == rtxdi::ReSTIRGI_ResamplingMode::TemporalAndSpatial)
            {
                commandList->beginMarker("GITemporalResampling");
                state.pipeline = m_giTemporalResamplingPipeline;
                commandList->setComputeState(state);
                commandList->dispatch(dispatchWidth, dispatchHeight);
                commandList->endMarker();
            }

            if (giMode == rtxdi::ReSTIRGI_ResamplingMode::Spatial ||
                giMode == rtxdi::ReSTIRGI_ResamplingMode::TemporalAndSpatial)
            {
                nvrhi::utils::BufferUavBarrier(commandList, m_giReservoirBuffer);

                commandList->beginMarker("GISpatialResampling");
                state.pipeline = m_giSpatialResamplingPipeline;
                commandList->setComputeState(state);
                commandList->dispatch(dispatchWidth, dispatchHeight);
                commandList->endMarker();
            }
        }

        nvrhi::utils::BufferUavBarrier(commandList, m_giReservoirBuffer);
        nvrhi::utils::BufferUavBarrier(commandList, m_secondaryGBuffer);

        commandList->beginMarker("GIFinalShading");
        state.pipeline = m_giFinalShadingPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight);
        commandList->endMarker();
    }

    nvrhi::utils::TextureUavBarrier(commandList, m_diffuseLightingTexture);
    nvrhi::utils::TextureUavBarrier(commandList, m_specularLightingTexture);

    commandList->beginMarker("Compositing");
    state.pipeline = m_compositingPipeline;
    commandList->setComputeState(state);
    commandList->dispatch(dispatchWidth, dispatchHeight);
    commandList->endMarker();
}

void LightingPasses::NextFrame()
{
    std::swap(m_bindingSet, m_prevBindingSet);
}
