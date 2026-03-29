/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
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
#include <Rtxdi/GI/ReSTIRGI.h>

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
            nvrhi::BindingSetItem::Texture_UAV(8, renderTargets.Emissive),
            nvrhi::BindingSetItem::Texture_UAV(9, renderTargets.SpecularLighting),
            nvrhi::BindingSetItem::Texture_UAV(10, renderTargets.HdrColor),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(11, resources.GIReservoirBuffer ? resources.GIReservoirBuffer : resources.LightReservoirBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(12, resources.SecondaryGBuffer ? resources.SecondaryGBuffer : resources.LightReservoirBuffer),
            
            nvrhi::BindingSetItem::ConstantBuffer(0, m_constantBuffer),
            nvrhi::BindingSetItem::Sampler(0, m_commonPasses->m_LinearWrapSampler),
            nvrhi::BindingSetItem::Sampler(1, m_commonPasses->m_LinearWrapSampler)
        };

        const nvrhi::BindingSetHandle bindingSet = m_device->createBindingSet(bindingSetDesc, m_bindingLayout);

        if (currentFrame)
            m_bindingSet = bindingSet;
        else
            m_prevBindingSet = bindingSet;
    }
    
    m_lightReservoirBuffer = resources.LightReservoirBuffer;
}

void LightingPasses::CreatePipelines()
{
    m_gbufferShader = m_shaderFactory->CreateShader("app/GBufferPass.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_initialSamplingShader = m_shaderFactory->CreateShader("app/DIGenerateInitialSamples.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_temporalResamplingShader = m_shaderFactory->CreateShader("app/DITemporalResampling.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_spatialResamplingShader = m_shaderFactory->CreateShader("app/DISpatialResampling.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_shadeSamplesShader = m_shaderFactory->CreateShader("app/DIShadeSamples.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_brdfRayTracingShader = m_shaderFactory->CreateShader("app/BrdfRayTracing.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_shadeSecondarySurfacesShader = m_shaderFactory->CreateShader("app/ShadeSecondarySurfaces.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_giTemporalResamplingShader = m_shaderFactory->CreateShader("app/GI/TemporalResampling.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_giSpatialResamplingShader = m_shaderFactory->CreateShader("app/GI/SpatialResampling.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_giFinalShadingShader = m_shaderFactory->CreateShader("app/GI/FinalShading.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    m_compositingShader = m_shaderFactory->CreateShader("app/Compositing.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    auto createPipeline = [this](nvrhi::IShader* shader) -> nvrhi::ComputePipelineHandle {
        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.bindingLayouts = { m_bindingLayout, m_bindlessLayout };
        pipelineDesc.CS = shader;
        return m_device->createComputePipeline(pipelineDesc);
    };

    m_gbufferPipeline = createPipeline(m_gbufferShader);
    m_initialSamplingPipeline = createPipeline(m_initialSamplingShader);
    m_temporalResamplingPipeline = createPipeline(m_temporalResamplingShader);
    m_spatialResamplingPipeline = createPipeline(m_spatialResamplingShader);
    m_shadeSamplesPipeline = createPipeline(m_shadeSamplesShader);
    m_brdfRayTracingPipeline = createPipeline(m_brdfRayTracingShader);
    m_shadeSecondarySurfacesPipeline = createPipeline(m_shadeSecondarySurfacesShader);
    m_giTemporalResamplingPipeline = createPipeline(m_giTemporalResamplingShader);
    m_giSpatialResamplingPipeline = createPipeline(m_giSpatialResamplingShader);
    m_giFinalShadingPipeline = createPipeline(m_giFinalShadingShader);
    m_compositingPipeline = createPipeline(m_compositingShader);
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
    context.SetResamplingMode(localSettings.enableResampling
        ? rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial
        : rtxdi::ReSTIRDI_ResamplingMode::None);

    ReSTIRDI_InitialSamplingParameters initialParams = context.GetInitialSamplingParameters();
    initialParams.numPrimaryLocalLightSamples = localSettings.numInitialSamples;
    initialParams.numPrimaryBrdfSamples = localSettings.numInitialBRDFSamples;
    initialParams.brdfCutoff = localSettings.brdfCutoff;
    initialParams.enableInitialVisibility = true;
    context.SetInitialSamplingParameters(initialParams);

    ReSTIRDI_TemporalResamplingParameters temporalParams = context.GetTemporalResamplingParameters();
    temporalParams.temporalBiasCorrection = localSettings.unbiasedMode
        ? ReSTIRDI_TemporalBiasCorrectionMode::Raytraced
        : ReSTIRDI_TemporalBiasCorrectionMode::Basic;
    context.SetTemporalResamplingParameters(temporalParams);

    ReSTIRDI_SpatialResamplingParameters spatialParams = context.GetSpatialResamplingParameters();
    spatialParams.numSpatialSamples = localSettings.numSpatialSamples;
    spatialParams.spatialBiasCorrection = localSettings.unbiasedMode
        ? ReSTIRDI_SpatialBiasCorrectionMode::Raytraced
        : ReSTIRDI_SpatialBiasCorrectionMode::Basic;
    context.SetSpatialResamplingParameters(spatialParams);

    ResamplingConstants constants = {};
    constants.frameIndex = context.GetFrameIndex();
    view.FillPlanarViewConstants(constants.view);
    previousView.FillPlanarViewConstants(constants.prevView);

    constants.runtimeParams = context.GetRuntimeParams();
    constants.lightBufferParams = lightBufferParams;

    constants.restirDI.reservoirBufferParams = context.GetReservoirBufferParameters();
    constants.restirDI.bufferIndices = context.GetBufferIndices();
    constants.restirDI.initialSamplingParams = context.GetInitialSamplingParameters();
    constants.restirDI.temporalResamplingParams = context.GetTemporalResamplingParameters();
    constants.restirDI.spatialResamplingParams = context.GetSpatialResamplingParameters();
    constants.restirDI.shadingParams = context.GetShadingParameters();

    constants.restirGI.reservoirBufferParams = giContext.GetReservoirBufferParameters();
    constants.restirGI.bufferIndices = giContext.GetBufferIndices();
    constants.restirGI.temporalResamplingParams = giContext.GetTemporalResamplingParameters();
    constants.restirGI.spatialResamplingParams = giContext.GetSpatialResamplingParameters();
    constants.restirGI.finalShadingParams = giContext.GetFinalShadingParameters();
    constants.brdfPT.enableReSTIRGI = localSettings.enableReSTIRGI ? 1 : 0;
    constants.enableBrdfIndirect = localSettings.enableReSTIRGI ? 1 : 0;

    constants.enableResampling = localSettings.enableResampling;

    commandList->writeBuffer(m_constantBuffer, &constants, sizeof(constants));

    const uint32_t dispatchWidth = dm::div_ceil(view.GetViewExtent().width(), RTXDI_SCREEN_SPACE_GROUP_SIZE);
    const uint32_t dispatchHeight = dm::div_ceil(view.GetViewExtent().height(), RTXDI_SCREEN_SPACE_GROUP_SIZE);

    nvrhi::ComputeState state;
    state.bindings = { m_bindingSet, m_scene->GetDescriptorTable() };

    // Pass 1: G-Buffer
    commandList->beginMarker("GBuffer");
    state.pipeline = m_gbufferPipeline;
    commandList->setComputeState(state);
    commandList->dispatch(dispatchWidth, dispatchHeight);
    commandList->endMarker();

    // UAV barrier so subsequent passes can read the G-buffer
    commandList->setResourceStatesForBindingSet(m_bindingSet);

    // Pass 2: Generate Initial Samples
    commandList->beginMarker("DIGenerateInitialSamples");
    state.pipeline = m_initialSamplingPipeline;
    commandList->setComputeState(state);
    commandList->dispatch(dispatchWidth, dispatchHeight);
    commandList->endMarker();

    if (localSettings.enableResampling)
    {
        // UAV barrier on reservoir buffer
        commandList->setResourceStatesForBindingSet(m_bindingSet);

        // Pass 3: Temporal Resampling
        commandList->beginMarker("DITemporalResampling");
        state.pipeline = m_temporalResamplingPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight);
        commandList->endMarker();

        commandList->setResourceStatesForBindingSet(m_bindingSet);

        // Pass 4: Spatial Resampling
        commandList->beginMarker("DISpatialResampling");
        state.pipeline = m_spatialResamplingPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight);
        commandList->endMarker();
    }

    // UAV barrier before shading
    commandList->setResourceStatesForBindingSet(m_bindingSet);

    // Pass 5: Shade Samples
    commandList->beginMarker("DIShadeSamples");
    state.pipeline = m_shadeSamplesPipeline;
    commandList->setComputeState(state);
    commandList->dispatch(dispatchWidth, dispatchHeight);
    commandList->endMarker();

    if (constants.enableBrdfIndirect)
    {
        commandList->setResourceStatesForBindingSet(m_bindingSet);

        commandList->beginMarker("BrdfRayTracing");
        state.pipeline = m_brdfRayTracingPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight);
        commandList->endMarker();

        commandList->setResourceStatesForBindingSet(m_bindingSet);

        commandList->beginMarker("ShadeSecondarySurfaces");
        state.pipeline = m_shadeSecondarySurfacesPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight);
        commandList->endMarker();

        commandList->setResourceStatesForBindingSet(m_bindingSet);

        commandList->beginMarker("GITemporalResampling");
        state.pipeline = m_giTemporalResamplingPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight);
        commandList->endMarker();

        commandList->setResourceStatesForBindingSet(m_bindingSet);

        commandList->beginMarker("GISpatialResampling");
        state.pipeline = m_giSpatialResamplingPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight);
        commandList->endMarker();

        commandList->setResourceStatesForBindingSet(m_bindingSet);

        commandList->beginMarker("GIFinalShading");
        state.pipeline = m_giFinalShadingPipeline;
        commandList->setComputeState(state);
        commandList->dispatch(dispatchWidth, dispatchHeight);
        commandList->endMarker();
    }

    commandList->setResourceStatesForBindingSet(m_bindingSet);

    // Compositing
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
