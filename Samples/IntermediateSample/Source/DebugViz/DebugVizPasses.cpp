/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "DebugVizPasses.h"

#include "../RenderTargets.h"

DebugVizPasses::DebugVizPasses(
    nvrhi::IDevice* device,
    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
    std::shared_ptr<donut::engine::Scene> scene,
    nvrhi::IBindingLayout* bindlessLayout) :
    m_gBufferNormalsViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_gBufferGeoNormalsViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_gBufferDiffuseAlbedoViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_gBufferSpecularRoughnessViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout))
{

}

void DebugVizPasses::CreatePipelines()
{
    m_gBufferNormalsViz->CreatePipeline("app/DebugViz/NDirOctUNorm32Viz.hlsl");
    m_gBufferGeoNormalsViz->CreatePipeline("app/DebugViz/NDirOctUNorm32Viz.hlsl");
    m_gBufferDiffuseAlbedoViz->CreatePipeline("app/DebugViz/PackedR11G11B10UFloatViz.hlsl");
    m_gBufferSpecularRoughnessViz->CreatePipeline("app/DebugViz/PackedR8G8B8A8GammaUFloatViz.hlsl");
}

void DebugVizPasses::CreateBindingSets(RenderTargets& renderTargets, nvrhi::TextureHandle dst)
{
    m_gBufferNormalsViz->CreateBindingSet(renderTargets.GBufferNormals, renderTargets.PrevGBufferNormals, renderTargets.DebugColor);
    m_gBufferGeoNormalsViz->CreateBindingSet(renderTargets.GBufferGeoNormals, renderTargets.PrevGBufferGeoNormals, renderTargets.DebugColor);
    m_gBufferDiffuseAlbedoViz->CreateBindingSet(renderTargets.GBufferDiffuseAlbedo, renderTargets.PrevGBufferDiffuseAlbedo, renderTargets.DebugColor);
    m_gBufferSpecularRoughnessViz->CreateBindingSet(renderTargets.GBufferSpecularRough, renderTargets.PrevGBufferSpecularRough, renderTargets.DebugColor);
}

void DebugVizPasses::RenderUnpackedNormals(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_gBufferNormalsViz->Render(commandList, view);
}

void DebugVizPasses::RenderUnpackedGeoNormals(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_gBufferGeoNormalsViz->Render(commandList, view);
}

void DebugVizPasses::RenderUnpackedDiffuseAlbeo(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_gBufferDiffuseAlbedoViz->Render(commandList, view);
}

void DebugVizPasses::RenderUnpackedSpecularRoughness(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_gBufferSpecularRoughnessViz->Render(commandList, view);
}

void DebugVizPasses::NextFrame()
{
    m_gBufferNormalsViz->NextFrame();
    m_gBufferGeoNormalsViz->NextFrame();
    m_gBufferDiffuseAlbedoViz->NextFrame();
    m_gBufferSpecularRoughnessViz->NextFrame();
}