/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#ifdef WITH_DLSS

#include <memory>
#include <nvrhi/nvrhi.h>

class RenderTargets;

namespace donut::engine
{
    class ShaderFactory;
    class PlanarView;
}

struct NVSDK_NGX_Handle;
struct NVSDK_NGX_Parameter;

class DLSS
{
public:
    DLSS(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory);

    [[nodiscard]] bool IsSupported() const;
    [[nodiscard]] bool IsAvailable() const;

    virtual void SetRenderSize(
        uint32_t inputWidth, uint32_t inputHeight,
        uint32_t outputWidth, uint32_t outputHeight) = 0;

    virtual void Render(
        nvrhi::ICommandList* commandList,
        const RenderTargets& renderTargets,
        nvrhi::IBuffer* toneMapperExposureBuffer,
        float exposureScale,
        float sharpness,
        bool gbufferWasRasterized,
        bool resetHistory,
        const donut::engine::PlanarView& view,
        const donut::engine::PlanarView& viewPrev) = 0;

    virtual ~DLSS() = default;

#if DONUT_WITH_DX12
    static std::unique_ptr<DLSS> CreateDX12(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory);
#endif
#if DONUT_WITH_VULKAN
    static std::unique_ptr<DLSS> CreateVK(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory);
#endif
    static void GetRequiredVulkanExtensions(std::vector<std::string>& instanceExtensions, std::vector<std::string>& deviceExtensions);

protected:
    bool m_featureSupported;
    bool m_isAvailable;

    NVSDK_NGX_Handle* m_dlssHandle;
    NVSDK_NGX_Parameter* m_parameters;

    // Use the AppID from the DLSS sample app until we get a separate one for RTXDI... wait, is it random?
    static const uint32_t c_applicationID = 231313132;

    uint32_t m_inputWidth;
    uint32_t m_inputHeight;
    uint32_t m_outputWidth;
    uint32_t m_outputHeight;

    nvrhi::DeviceHandle m_device;
    nvrhi::ShaderHandle m_exposureShader;
    nvrhi::ComputePipelineHandle m_exposurePipeline;
    nvrhi::TextureHandle m_exposureTexture;
    nvrhi::BufferHandle m_exposureSourceBuffer;
    nvrhi::BindingLayoutHandle m_exposureBindingLayout;
    nvrhi::BindingSetHandle m_exposureBindingSet;
    nvrhi::CommandListHandle m_featureCommandList;

    void ComputeExposure(nvrhi::ICommandList* commandList, nvrhi::IBuffer* toneMapperExposureBuffer, float exposureScale);
};

#endif
