/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

// Include this first just to test the cleanliness
#include <Rtxdi/DI/ReSTIRDI.h>
#include <Rtxdi/GI/ReSTIRGI.h>

#include <donut/app/ApplicationBase.h>
#include <donut/app/Camera.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/BindingCache.h>
#include <donut/engine/Scene.h>
#include <donut/engine/DescriptorTableManager.h>
#include <donut/engine/View.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/math/math.h>
#include <nvrhi/utils.h>
#include <GLFW/glfw3.h>
#include <cstring>

#include "RenderTargets.h"
#include "PrepareLightsPass.h"
#include "LightingPasses.h"
#include "AccumulationPass.h"
#include "RenderEnvironmentMapPass.h"
#include "GenerateMipsPass.h"
#include "RtxdiResources.h"
#include "SampleScene.h"
#include "UserInterface.h"
#include "Testing.h"

#include <Rtxdi/LightSampling/RISBufferSegmentAllocator.h>
#include <donut/engine/SceneGraph.h>
#include <donut/render/SkyPass.h>

#ifndef _WIN32
#include <unistd.h>
#else
extern "C" {
  // Prefer using the discrete GPU on Optimus laptops
  _declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}
#endif

using namespace donut;
using namespace donut::math;
using namespace std::chrono;

static int g_ExitCode = 0;

class SceneRenderer : public app::ApplicationBase
{
public:
    SceneRenderer(app::DeviceManager* deviceManager, UIData& ui, const CommandLineArguments& args)
        : ApplicationBase(deviceManager)
        , m_bindingCache(deviceManager->GetDevice())
        , m_ui(ui)
        , m_args(args)
    { 
    }

    [[nodiscard]] std::shared_ptr<engine::ShaderFactory> GetShaderFactory() const
    {
        return m_shaderFactory;
    }

    [[nodiscard]] std::shared_ptr<vfs::IFileSystem> GetRootFs() const
    {
        return m_rootFs;
    }

    bool Init()
    {
        std::filesystem::path mediaPath = app::GetDirectoryWithExecutable().parent_path() / "Assets/Media";
        if (!std::filesystem::exists(mediaPath))
        {
            mediaPath = app::GetDirectoryWithExecutable().parent_path().parent_path() / "Assets/Media";
            if (!std::filesystem::exists(mediaPath))
            {
                log::error("Couldn't locate the 'Assets/Media' folder.");
                return false;
            }
        }

        std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
        std::filesystem::path appShaderPath = app::GetDirectoryWithExecutable() / "shaders/minimal-gi-sample" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

        log::debug("Mounting %s to %s", mediaPath.string().c_str(), "/Assets/Media");
        log::debug("Mounting %s to %s", frameworkShaderPath.string().c_str(), "/shaders/donut");
        log::debug("Mounting %s to %s", appShaderPath.string().c_str(), "/shaders/app");

        m_rootFs = std::make_shared<vfs::RootFileSystem>();
        m_rootFs->mount("/Assets/Media", mediaPath);
        m_rootFs->mount("/shaders/donut", frameworkShaderPath);
        m_rootFs->mount("/shaders/app", appShaderPath);

        m_shaderFactory = std::make_shared<engine::ShaderFactory>(GetDevice(), m_rootFs, "/shaders");
        m_CommonPasses = std::make_shared<engine::CommonRenderPasses>(GetDevice(), m_shaderFactory);

        {
            nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
            bindlessLayoutDesc.firstSlot = 0;
            bindlessLayoutDesc.registerSpaces = {
                nvrhi::BindingLayoutItem::RawBuffer_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_UAV(3)
            };
            bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
            bindlessLayoutDesc.maxCapacity = 1024;
            m_bindlessLayout = GetDevice()->createBindlessLayout(bindlessLayoutDesc);
        }

        std::filesystem::path scenePath = m_args.scenePath.empty()
            ? "/Assets/Media/Arcade/Arcade.gltf"
            : m_args.scenePath;

        m_descriptorTableManager = std::make_shared<engine::DescriptorTableManager>(GetDevice(), m_bindlessLayout);

        m_TextureCache = std::make_shared<donut::engine::TextureCache>(GetDevice(), m_rootFs, m_descriptorTableManager);
        m_TextureCache->SetInfoLogSeverity(donut::log::Severity::Debug);
        
        m_scene = std::make_shared<SampleScene>(GetDevice(), *m_shaderFactory, m_rootFs, m_TextureCache, m_descriptorTableManager, nullptr);
        
        SetAsynchronousLoadingEnabled(true);
        BeginLoadingScene(m_rootFs, scenePath);
        GetDeviceManager()->SetVsyncEnabled(true);

        m_prepareLightsPass = std::make_unique<PrepareLightsPass>(GetDevice(), m_shaderFactory, m_CommonPasses, m_scene, m_bindlessLayout);
        m_lightingPasses = std::make_unique<LightingPasses>(GetDevice(), m_shaderFactory, m_CommonPasses, m_scene, m_bindlessLayout);
        m_accumulationPass = std::make_unique<AccumulationPass>(GetDevice(), m_shaderFactory);


        LoadShaders();
        
        m_commandList = GetDevice()->createCommandList();

        return true;
    }
    
    void InitCameraFromScene()
    {
        if (m_args.scenePath.empty())
        {
            m_camera.LookAt(float3(-1.658f, 1.577f, 1.69f), float3(-0.9645f, 1.2672f, 1.0396f));
        }
        else
        {
            auto* root = m_scene->GetSceneGraph()->GetRootNode().get();
            for (size_t i = 0; i < root->GetNumChildren() && !m_cameraInitialized; i++)
                InitCameraFromNode(root->GetChild(i));

            if (!m_cameraInitialized)
                m_camera.LookAt(float3(0.f, 1.5f, 3.f), float3(0.f, 1.0f, 0.f));
        }
        m_camera.SetMoveSpeed(3.f);
    }

    void InitCameraFromNode(engine::SceneGraphNode* node)
    {
        if (m_cameraInitialized || !node)
            return;

        auto camera = std::dynamic_pointer_cast<engine::PerspectiveCamera>(node->GetLeaf());
        if (camera)
        {
            dm::affine3 viewToWorld = camera->GetViewToWorldMatrix();
            float3 pos = float3(viewToWorld.m_translation);
            float3 forward = float3(-viewToWorld.m_linear.row2);
            m_camera.LookAt(pos, pos + forward);
            m_cameraInitialized = true;
            return;
        }

        for (size_t i = 0; i < node->GetNumChildren() && !m_cameraInitialized; i++)
            InitCameraFromNode(node->GetChild(i));
    }

    void SceneLoaded() override
    {
        ApplicationBase::SceneLoaded();

        m_scene->FinishedLoading(GetFrameIndex());
        
        InitCameraFromScene();
        
        m_scene->BuildMeshBLASes(GetDevice());

        m_commandList->open();
        m_scene->BuildTopLevelAccelStruct(m_commandList);
        m_commandList->close();
        GetDevice()->executeCommandList(m_commandList);

        GetDeviceManager()->SetVsyncEnabled(false);

        m_ui.isLoading = false;
    }
    
    void LoadShaders() const
    {
        m_prepareLightsPass->CreatePipeline();
        m_lightingPasses->CreatePipeline();
        m_accumulationPass->CreatePipeline();
    }

    bool LoadScene(std::shared_ptr<vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName) override 
    {
        if (m_scene->Load(sceneFileName))
        {
            return true;
        }

        return false;
    }

    bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
        if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS)
        {
            m_ui.showUI = !m_ui.showUI;
            return true;
        }

        if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_R && action == GLFW_PRESS)
        {
            m_ui.reloadShaders = true;
            return true;
        }

        m_camera.KeyboardUpdate(key, scancode, action, mods);

        return true;
    }

    bool MousePosUpdate(double xpos, double ypos) override
    {
        m_camera.MousePosUpdate(xpos, ypos);
        return true;
    }

    bool MouseButtonUpdate(int button, int action, int mods) override
    {
        m_camera.MouseButtonUpdate(button, action, mods);
        return true;
    }

    void Animate(float fElapsedTimeSeconds) override
    {
        if (m_ui.isLoading)
            return;

        if (!m_args.saveFrameFileName.empty() || !m_args.compareBaselinePath.empty())
            fElapsedTimeSeconds = 1.f / 60.f;

        m_camera.Animate(fElapsedTimeSeconds);
    }

    void BackBufferResized(const uint32_t width, const uint32_t height, const uint32_t sampleCount) override
    {
        if (m_renderTargets && m_renderTargets->Size.x == int(width) && m_renderTargets->Size.y == int(height))
            return;

        m_bindingCache.Clear();
        m_renderTargets = nullptr;
        m_restirDIContext = nullptr;
        m_restirGIContext = nullptr;
        m_rtxdiResources = nullptr;
        m_environmentMapPdfMipmapPass = nullptr;
        m_risBufferSegmentAllocator = nullptr;
    }
    
    void SetupView(const nvrhi::FramebufferInfoEx& fbinfo, uint effectiveFrameIndex)
    {
        nvrhi::Viewport windowViewport(float(fbinfo.width), float(fbinfo.height));

        nvrhi::Viewport renderViewport = windowViewport;
        m_view.SetViewport(renderViewport);
        m_view.SetPixelOffset(0.f);

        const float aspectRatio = windowViewport.width() / windowViewport.height();
        m_view.SetMatrices(m_camera.GetWorldToViewMatrix(), perspProjD3DStyleReverse(radians(60.f), aspectRatio, 0.01f));
        m_view.UpdateCache();

        if (m_viewPrevious.GetViewExtent().width() == 0)
            m_viewPrevious = m_view;
    }

    void SetupRenderPasses(const nvrhi::FramebufferInfoEx& fbinfo)
    {
        if (m_ui.reloadShaders)
        {
            GetDevice()->waitForIdle();

            m_shaderFactory->ClearCache();
            m_renderEnvironmentMapPass = nullptr;
            m_environmentMapPdfMipmapPass = nullptr;
            m_environmentMapDirty = true;
            
            LoadShaders();

            m_ui.reloadShaders = false;
        }

        bool renderTargetsCreated = false;
        bool rtxdiResourcesCreated = false;
        
        if (!m_restirDIContext)
        {
            rtxdi::ReSTIRDIStaticParameters contextParams;
            contextParams.RenderWidth = fbinfo.width;
            contextParams.RenderHeight = fbinfo.height;

            m_restirDIContext = std::make_unique<rtxdi::ReSTIRDIContext>(contextParams);

            rtxdi::ReSTIRGIStaticParameters giParams;
            giParams.RenderWidth = fbinfo.width;
            giParams.RenderHeight = fbinfo.height;

            m_restirGIContext = std::make_unique<rtxdi::ReSTIRGIContext>(giParams);
        }

        if (!m_renderEnvironmentMapPass && m_environmentLight)
        {
            m_renderEnvironmentMapPass = std::make_unique<RenderEnvironmentMapPass>(
                GetDevice(), m_shaderFactory, m_descriptorTableManager, 2048);
            m_environmentMapDirty = true;
        }

        nvrhi::ITexture* environmentMap = m_renderEnvironmentMapPass
            ? m_renderEnvironmentMapPass->GetTexture() : nullptr;

        uint2 environmentMapSize = environmentMap
            ? uint2(environmentMap->getDesc().width, environmentMap->getDesc().height)
            : uint2(1, 1);

        if (!m_renderTargets)
        {
            m_renderTargets = std::make_unique<RenderTargets>(GetDevice(), int2(fbinfo.width, fbinfo.height));

            renderTargetsCreated = true;
        }

        if (!m_rtxdiResources)
        {
            uint32_t numEmissiveMeshes, numEmissiveTriangles;
            m_prepareLightsPass->CountLightsInScene(numEmissiveMeshes, numEmissiveTriangles);
            uint32_t numGeometryInstances = uint32_t(m_scene->GetSceneGraph()->GetGeometryInstancesCount());
            uint32_t numPrimitiveLights = uint32_t(m_scene->GetSceneGraph()->GetLights().size());

            const uint32_t risTileSize = 1024;
            const uint32_t risTileCount = 128;
            m_risBufferSegmentAllocator = std::make_unique<rtxdi::RISBufferSegmentAllocator>();
            uint32_t localLightOffset = m_risBufferSegmentAllocator->allocateSegment(risTileCount * risTileSize);
            uint32_t envLightOffset = m_risBufferSegmentAllocator->allocateSegment(risTileCount * risTileSize);
            m_localLightsRISBufferSegmentParams = { localLightOffset, risTileSize, risTileCount, 0 };
            m_environmentLightRISBufferSegmentParams = { envLightOffset, risTileSize, risTileCount, 0 };

            m_rtxdiResources = std::make_unique<RtxdiResources>(GetDevice(), *m_restirDIContext,
                *m_risBufferSegmentAllocator,
                numEmissiveMeshes, numEmissiveTriangles,
                numPrimitiveLights, numGeometryInstances,
                environmentMapSize.x, environmentMapSize.y);

            m_prepareLightsPass->CreateBindingSet(*m_rtxdiResources);
            
            rtxdiResourcesCreated = true;
        }

        if (m_renderEnvironmentMapPass && (!m_environmentMapPdfMipmapPass || rtxdiResourcesCreated))
        {
            m_environmentMapPdfMipmapPass = std::make_unique<GenerateMipsPass>(
                GetDevice(), m_shaderFactory,
                environmentMap,
                m_rtxdiResources->EnvironmentPdfTexture);
            m_environmentMapDirty = true;
        }
        
        if (renderTargetsCreated || rtxdiResourcesCreated)
        {
            m_lightingPasses->CreateBindingSet(
                m_scene->GetTopLevelAS(),
                *m_renderTargets,
                *m_rtxdiResources);

            m_accumulationPass->CreateBindingSet(*m_renderTargets);
        }
    }

    void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override
    {
        m_commandList->open();
        nvrhi::utils::ClearColorAttachment(m_commandList, framebuffer, 0, nvrhi::Color(0.f));
        m_commandList->close();
        GetDevice()->executeCommandList(m_commandList);
    }
    
    void RenderScene(nvrhi::IFramebuffer* framebuffer) override
    {   
        const auto& fbinfo = framebuffer->getFramebufferInfo();

        SetupView(fbinfo, GetFrameIndex());
        SetupRenderPasses(fbinfo);
        
        m_commandList->open();

        m_scene->Refresh(m_commandList, GetFrameIndex());

        m_rtxdiResources->InitializeNeighborOffsets(m_commandList, m_restirDIContext->GetStaticParameters().NeighborOffsetCount);
        
        m_restirDIContext->SetFrameIndex(GetFrameIndex());
        m_restirGIContext->SetFrameIndex(GetFrameIndex());

        if (m_environmentLight && m_renderEnvironmentMapPass)
        {
            m_environmentLight->textureIndex = m_renderEnvironmentMapPass->GetTextureIndex();
            const auto& texDesc = m_renderEnvironmentMapPass->GetTexture()->getDesc();
            m_environmentLight->textureSize = uint2(texDesc.width, texDesc.height);
            m_environmentLight->radianceScale = 1.f;
            m_environmentLight->rotation = 0.f;
            m_sunLight->irradiance = 1.f;
        }

        if (m_environmentMapDirty && m_renderEnvironmentMapPass && m_sunLight)
        {
            donut::render::SkyParameters skyParams;
            m_renderEnvironmentMapPass->Render(m_commandList, *m_sunLight, skyParams);

            if (m_environmentMapPdfMipmapPass)
                m_environmentMapPdfMipmapPass->Process(m_commandList);

            m_environmentMapDirty = false;
        }

        bool hasEnvironmentMap = m_environmentLight && m_renderEnvironmentMapPass;
        RTXDI_LightBufferParameters lightBufferParams = m_prepareLightsPass->Process(
            m_commandList, *m_restirDIContext,
            m_scene->GetSceneGraph()->GetLights(),
            hasEnvironmentMap);

        LightingPasses::EnvironmentRenderParams envParams;
        if (hasEnvironmentMap)
        {
            envParams.sceneConstants.enableEnvironmentMap = 1;
            envParams.sceneConstants.environmentMapTextureIndex = m_renderEnvironmentMapPass->GetTextureIndex();
            envParams.sceneConstants.environmentScale = m_environmentLight->radianceScale.x;
            envParams.sceneConstants.environmentRotation = m_environmentLight->rotation;

            envParams.environmentPdfTextureSize = uint2(
                m_rtxdiResources->EnvironmentPdfTexture->getDesc().width,
                m_rtxdiResources->EnvironmentPdfTexture->getDesc().height);
            envParams.localLightPdfTextureSize = uint2(
                m_rtxdiResources->LocalLightPdfTexture->getDesc().width,
                m_rtxdiResources->LocalLightPdfTexture->getDesc().height);
        }
        envParams.localLightsRISBufferSegmentParams = m_localLightsRISBufferSegmentParams;
        envParams.environmentLightRISBufferSegmentParams = m_environmentLightRISBufferSegmentParams;

        m_lightingPasses->Render(m_commandList,
            *m_restirDIContext,
            *m_restirGIContext,
            m_view, m_viewPrevious,
            m_ui.lightingSettings,
            lightBufferParams,
            envParams);

        nvrhi::ITexture* displayTexture = m_renderTargets->HdrColor;

        if (m_ui.enableAccumulation)
        {
            bool cameraIsStatic = m_view.GetViewMatrix() == m_viewPrevious.GetViewMatrix();
            if (cameraIsStatic && !m_ui.resetAccumulation)
                m_ui.numAccumulatedFrames += 1;
            else
                m_ui.numAccumulatedFrames = 1;

            float accumulationWeight = 1.f / (float)m_ui.numAccumulatedFrames;

            m_accumulationPass->Render(m_commandList, m_view, m_view, accumulationWeight);
            displayTexture = m_renderTargets->AccumulatedColor;
            m_ui.resetAccumulation = false;
        }

        m_CommonPasses->BlitTexture(m_commandList, framebuffer, displayTexture, &m_bindingCache);
        
        m_commandList->close();
        GetDevice()->executeCommandList(m_commandList);

        bool wantsSave = !m_args.saveFrameFileName.empty();
        bool wantsCompare = !m_args.compareBaselinePath.empty();

        if ((wantsSave || wantsCompare) && m_renderFrameIndex == m_args.saveFrameIndex)
        {
            nvrhi::ITexture* backBuffer = framebuffer->getDesc().colorAttachments[0].texture;

            if (wantsSave)
            {
                bool success = SaveTexture(GetDevice(), backBuffer, m_args.saveFrameFileName.c_str());
                g_ExitCode = success ? 0 : 1;
            }

            if (wantsCompare)
            {
                nvrhi::TextureDesc desc = backBuffer->getDesc();
                nvrhi::CommandListHandle readbackCmdList = GetDevice()->createCommandList();
                readbackCmdList->open();
                nvrhi::StagingTextureHandle staging =
                    GetDevice()->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
                readbackCmdList->copyTexture(staging, nvrhi::TextureSlice(), backBuffer, nvrhi::TextureSlice());
                readbackCmdList->close();
                GetDevice()->executeCommandList(readbackCmdList);
                GetDevice()->waitForIdle();

                size_t rowPitch = 0;
                void* pData = GetDevice()->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);
                std::vector<uint8_t> rendered(desc.width * desc.height * 4);
                if (pData)
                {
                    for (uint32_t row = 0; row < desc.height; row++)
                        memcpy(rendered.data() + row * desc.width * 4,
                               static_cast<const char*>(pData) + row * rowPitch,
                               desc.width * 4);
                    GetDevice()->unmapStagingTexture(staging);
                }

                if (!wantsSave)
                {
                    SaveTexture(GetDevice(), backBuffer, "test_output.bmp");
                }

                size_t bW = 0, bH = 0;
                auto baseline = LoadImageRGBA8(m_args.compareBaselinePath, bW, bH);
                if (baseline.empty())
                {
                    log::error("Failed to load baseline image: %s", m_args.compareBaselinePath.c_str());
                    g_ExitCode = 1;
                }
                else if (bW != desc.width || bH != desc.height)
                {
                    log::error("Baseline size mismatch: baseline %zux%zu vs rendered %ux%u",
                        bW, bH, desc.width, desc.height);
                    g_ExitCode = 1;
                }
                else
                {
                    StochasticThresholds thresholds;
                    thresholds.tileSize = 32;
                    thresholds.absAvgDeltaThreshold = 0.05f;
                    thresholds.relDifferenceThreshold = 0.08f;
                    thresholds.absStdDevDeltaThreshold = 0.05f;

                    StochasticResult result = CompareStochastic(
                        rendered.data(), baseline.data(), desc.width, desc.height, thresholds);

                    log::info("%s", result.summary.c_str());

                    g_ExitCode = result.passed ? 0 : 1;
                    if (result.passed)
                        log::info("Baseline comparison PASSED.");
                    else
                        log::error("Baseline comparison FAILED.");
                }
            }

            glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), 1);
        }

        m_lightingPasses->NextFrame();
        m_renderTargets->NextFrame();

        m_viewPrevious = m_view;
        m_renderFrameIndex++;
    }

private:
    nvrhi::CommandListHandle m_commandList;

    nvrhi::BindingLayoutHandle m_bindlessLayout;

    std::shared_ptr<vfs::RootFileSystem> m_rootFs;
    std::shared_ptr<engine::ShaderFactory> m_shaderFactory;
    std::shared_ptr<SampleScene> m_scene;
    std::shared_ptr<engine::DescriptorTableManager> m_descriptorTableManager;
    std::unique_ptr<RenderTargets> m_renderTargets;
    app::FirstPersonCamera m_camera;
    engine::PlanarView m_view;
    engine::PlanarView m_viewPrevious;
    engine::BindingCache m_bindingCache;

    std::unique_ptr<rtxdi::ReSTIRDIContext> m_restirDIContext;
    std::unique_ptr<rtxdi::ReSTIRGIContext> m_restirGIContext;
    std::unique_ptr<PrepareLightsPass> m_prepareLightsPass;
    std::unique_ptr<LightingPasses> m_lightingPasses;
    std::unique_ptr<AccumulationPass> m_accumulationPass;
    std::unique_ptr<RtxdiResources> m_rtxdiResources;
    std::unique_ptr<RenderEnvironmentMapPass> m_renderEnvironmentMapPass;
    std::unique_ptr<GenerateMipsPass> m_environmentMapPdfMipmapPass;
    std::unique_ptr<rtxdi::RISBufferSegmentAllocator> m_risBufferSegmentAllocator;

    std::shared_ptr<donut::engine::DirectionalLight> m_sunLight;
    std::shared_ptr<EnvironmentLight> m_environmentLight;

    RTXDI_RISBufferSegmentParameters m_localLightsRISBufferSegmentParams = {};
    RTXDI_RISBufferSegmentParameters m_environmentLightRISBufferSegmentParams = {};

    UIData& m_ui;
    CommandLineArguments m_args;
    bool m_cameraInitialized = false;
    bool m_environmentMapDirty = true;
    uint32_t m_renderFrameIndex = 0;
};

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int argc, char** argv)
#endif
{
    app::DeviceCreationParameters deviceParams;
    deviceParams.swapChainBufferCount = 3;
    deviceParams.enableRayTracingExtensions = true;
    deviceParams.backBufferWidth = 1920;
    deviceParams.backBufferHeight = 1080;
    deviceParams.vsyncEnabled = true;
    deviceParams.infoLogSeverity = log::Severity::Debug;

    UIData ui;
    CommandLineArguments args;

#if DONUT_WITH_DX12
    args.graphicsApi = nvrhi::GraphicsAPI::D3D12;
#else
    args.graphicsApi = nvrhi::GraphicsAPI::VULKAN;
#endif

#if defined(_WIN32)
    ProcessCommandLine(__argc, __argv, deviceParams, ui, args);
#else
    ProcessCommandLine(argc, argv, deviceParams, ui, args);
#endif

    ui.lightingSettings.diagMode = args.diagMode;

    app::DeviceManager* deviceManager = app::DeviceManager::Create(args.graphicsApi);
    
    const char* apiString = nvrhi::utils::GraphicsAPIToString(deviceManager->GetGraphicsAPI());

    std::string windowTitle = "RTXDI Minimal GI Sample (" + std::string(apiString) + ")";
    
    log::SetErrorMessageCaption(windowTitle.c_str());

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, windowTitle.c_str()))
    {
        log::error("Cannot initialize a %s graphics device.", apiString);
        return 1;
    }
    
    bool rayQuerySupported = deviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::RayQuery);

    if (!rayQuerySupported)
    {
        log::error("The GPU (%s) or its driver does not support Ray Queries.", deviceManager->GetRendererString());
        return 1;
    }

    {
        SceneRenderer sceneRenderer(deviceManager, ui, args);
        if (sceneRenderer.Init())
        {
            UserInterface userInterface(deviceManager, *sceneRenderer.GetRootFs(), ui);
            userInterface.Init(sceneRenderer.GetShaderFactory());

            deviceManager->AddRenderPassToBack(&sceneRenderer);
            deviceManager->AddRenderPassToBack(&userInterface);
            deviceManager->RunMessageLoop();
            deviceManager->GetDevice()->waitForIdle();
            deviceManager->RemoveRenderPass(&sceneRenderer);
            deviceManager->RemoveRenderPass(&userInterface);
        }
    }
    
    deviceManager->Shutdown();

    delete deviceManager;

    return g_ExitCode;
}
