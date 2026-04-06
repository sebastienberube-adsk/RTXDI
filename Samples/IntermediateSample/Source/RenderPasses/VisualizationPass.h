/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include "RayTracingPass.h"

#include <nvrhi/nvrhi.h>
#include <memory>

namespace rtxdi
{
    class ImportanceSamplingContext;
}

namespace donut::engine
{
    class IView;
    class ShaderFactory;
    class CommonRenderPasses;
}

class RenderTargets;
class RtxdiResources;

class VisualizationPass
{
public:
    VisualizationPass(
        nvrhi::IDevice* device,
        donut::engine::CommonRenderPasses& commonPasses,
        donut::engine::ShaderFactory& shaderFactory,
        RenderTargets& renderTargets,
        RtxdiResources& rtxdiResources);

    void Render(
        nvrhi::ICommandList* commandList,
        nvrhi::IFramebuffer* framebuffer,
        const donut::engine::IView& renderView,
        const donut::engine::IView& upscaledView,
        const rtxdi::ImportanceSamplingContext& context,
        uint32_t inputBufferIndex,
        uint32_t visualizationMode,
        bool enableAccumulation);

    void NextFrame();

private:
    nvrhi::DeviceHandle m_device;

    nvrhi::BindingLayoutHandle m_hdrBindingLayout;
    nvrhi::BindingLayoutHandle m_confidenceBindingLayout;
    nvrhi::BindingSetHandle m_hdrBindingSet;
    nvrhi::BindingSetHandle m_confidenceBindingSet;
    nvrhi::BindingSetHandle m_confidenceBindingSetPrev;
    nvrhi::ShaderHandle m_vertexShader;
    nvrhi::ShaderHandle m_hdrPixelShader;
    nvrhi::ShaderHandle m_confidencePixelShader;
    nvrhi::GraphicsPipelineHandle m_hdrPipeline;
    nvrhi::GraphicsPipelineHandle m_confidencePipeline;

    nvrhi::BufferHandle m_constantBuffer;
};
