/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#if WITH_DLSS

#include "DLSS.h"
#include "RenderTargets.h"
#include <donut/engine/ShaderFactory.h>

using namespace donut;

DLSS::DLSS(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory) :
    m_featureSupported(false),
    m_isAvailable(false),
    m_dlssHandle(nullptr),
    m_parameters(nullptr),
    m_inputWidth(0),
    m_inputHeight(0),
    m_outputWidth(0),
    m_outputHeight(0),
    m_device(device)
{
    m_exposureShader = shaderFactory.CreateShader("app/DlssExposure.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    auto layoutDesc = nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute)
        .addItem(nvrhi::BindingLayoutItem::TypedBuffer_SRV(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0))
        .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(float)));

    m_exposureBindingLayout = device->createBindingLayout(layoutDesc);

    auto pipelineDesc = nvrhi::ComputePipelineDesc()
        .addBindingLayout(m_exposureBindingLayout)
        .setComputeShader(m_exposureShader);

    m_exposurePipeline = device->createComputePipeline(pipelineDesc);

    auto textureDesc = nvrhi::TextureDesc()
        .setWidth(1)
        .setHeight(1)
        .setFormat(nvrhi::Format::R32_FLOAT)
        .setDebugName("DLSS Exposure Texture")
        .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
        .setKeepInitialState(true)
        .setDimension(nvrhi::TextureDimension::Texture2D)
        .setIsUAV(true);

    m_exposureTexture = device->createTexture(textureDesc);

    m_featureCommandList = device->createCommandList();
}

bool DLSS::IsSupported() const
{
    return m_featureSupported;
}

bool DLSS::IsAvailable() const
{
    return m_featureSupported && m_isAvailable;
}

void DLSS::ComputeExposure(nvrhi::ICommandList* commandList, nvrhi::IBuffer* toneMapperExposureBuffer, float exposureScale)
{
    if (m_exposureSourceBuffer != toneMapperExposureBuffer)
    {
        m_exposureSourceBuffer = nullptr;
        m_exposureBindingSet = nullptr;
    }

    if (!m_exposureBindingSet)
    {
        auto setDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(0, toneMapperExposureBuffer))
            .addItem(nvrhi::BindingSetItem::Texture_UAV(0, m_exposureTexture))
            .addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(float)));

        m_exposureBindingSet = m_device->createBindingSet(setDesc, m_exposureBindingLayout);
    }

    auto state = nvrhi::ComputeState()
        .setPipeline(m_exposurePipeline)
        .addBindingSet(m_exposureBindingSet);

    commandList->setComputeState(state);
    commandList->setPushConstants(&exposureScale, sizeof(float));
    commandList->dispatch(1);
}

#endif
