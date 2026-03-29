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
#include <cxxopts.hpp>
#include <stb_image_write.h>

#include "RenderTargets.h"
#include "PrepareLightsPass.h"
#include "RenderPass.h"
#include "RtxdiResources.h"
#include "SampleScene.h"
#include "UserInterface.h"

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
namespace fs = std::filesystem;

static int g_ExitCode = 0;

struct CommandLineArguments
{
    nvrhi::GraphicsAPI graphicsApi = nvrhi::GraphicsAPI::D3D12;
    uint32_t saveFrameIndex = 0;
    std::string saveFrameFileName;
    std::string scenePath;
};

static bool SaveTexture(nvrhi::IDevice* device, nvrhi::ITexture* texture, const char* writeFileName)
{
    nvrhi::TextureDesc desc = texture->getDesc();

    nvrhi::CommandListHandle commandList = device->createCommandList();
    commandList->open();

    nvrhi::StagingTextureHandle stagingTexture =
        device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
    commandList->copyTexture(stagingTexture, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice());

    commandList->close();
    device->executeCommandList(commandList);
    device->waitForIdle();

    size_t rowPitch = 0;
    void* pData = device->mapStagingTexture(
        stagingTexture, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);

    if (!pData)
    {
        log::error("Couldn't map the readback texture.");
        return false;
    }

    std::vector<uint8_t> packedPixels(desc.width * desc.height * 4);
    for (uint32_t row = 0; row < desc.height; row++)
    {
        memcpy(
            packedPixels.data() + row * desc.width * 4,
            static_cast<const char*>(pData) + row * rowPitch,
            desc.width * 4);
    }

    device->unmapStagingTexture(stagingTexture);

    if (desc.format == nvrhi::Format::BGRA8_UNORM || desc.format == nvrhi::Format::SBGRA8_UNORM)
    {
        for (size_t i = 0; i < packedPixels.size(); i += 4)
            std::swap(packedPixels[i], packedPixels[i + 2]);
    }

    bool success = true;
    if (writeFileName && *writeFileName)
    {
        fs::path parentFolder = fs::path(writeFileName).parent_path();
        if (!parentFolder.empty() && !fs::exists(parentFolder))
        {
            log::info("Creating folder '%s'", parentFolder.generic_string().c_str());
            fs::create_directories(parentFolder);
        }

        success = stbi_write_bmp(writeFileName, desc.width, desc.height, 4, packedPixels.data()) != 0;
        if (success)
            log::info("Saved screenshot to '%s'", writeFileName);
        else
            log::error("Failed to save screenshot to '%s'", writeFileName);
    }

    return success;
}

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
        std::filesystem::path appShaderPath = app::GetDirectoryWithExecutable() / "shaders/minimal-sample" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

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
        m_renderPass = std::make_unique<RenderPass>(GetDevice(), m_shaderFactory, m_CommonPasses, m_scene, m_bindlessLayout);


        LoadShaders();
        
        m_commandList = GetDevice()->createCommandList();

        return true;
    }
    
    void InitCameraFromScene()
    {
        auto* root = m_scene->GetSceneGraph()->GetRootNode().get();
        for (size_t i = 0; i < root->GetNumChildren() && !m_cameraInitialized; i++)
            InitCameraFromNode(root->GetChild(i));

        if (!m_cameraInitialized)
        {
            if (m_args.scenePath.empty())
                m_camera.LookAt(float3(-1.658f, 1.577f, 1.69f), float3(-0.9645f, 1.2672f, 1.0396f));
            else
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
        m_renderPass->CreatePipeline();
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

        if (!m_args.saveFrameFileName.empty())
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
        m_rtxdiResources = nullptr;
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
        }

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

            m_rtxdiResources = std::make_unique<RtxdiResources>(GetDevice(), *m_restirDIContext,
                numEmissiveMeshes, numEmissiveTriangles, numGeometryInstances);

            m_prepareLightsPass->CreateBindingSet(*m_rtxdiResources);
            
            rtxdiResourcesCreated = true;
        }
        
        if (renderTargetsCreated || rtxdiResourcesCreated)
        {
            m_renderPass->CreateBindingSet(
                m_scene->GetTopLevelAS(),
                *m_renderTargets,
                *m_rtxdiResources);
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

        // Setup the viewports and transforms
        SetupView(fbinfo, GetFrameIndex());

        // Make sure that the passes and buffers are created and fit the current render size
        SetupRenderPasses(fbinfo);
        
        m_commandList->open();

        // Compute transforms, update the scene representation on the GPU in case something's animated
        m_scene->Refresh(m_commandList, GetFrameIndex());

        // Write the neighbor offset buffer data (only happens once)
        m_rtxdiResources->InitializeNeighborOffsets(m_commandList, m_restirDIContext->GetStaticParameters().NeighborOffsetCount);
        
        // The light indexing members of frameParameters are written by PrepareLightsPass below
        m_restirDIContext->SetFrameIndex(GetFrameIndex());

        // When the lights are static, there is no need to update them on every frame,
        // but it's simpler to do so.
        RTXDI_LightBufferParameters lightBufferParams = m_prepareLightsPass->Process(m_commandList);

        // Call the rendering pass - this includes primary rays, fused resampling, and shading
        m_renderPass->Render(m_commandList,
            *m_restirDIContext,
            m_view, m_viewPrevious,
            m_ui.lightingSettings,
            lightBufferParams);

        // Copy the render pass output to the swap chain
        m_CommonPasses->BlitTexture(m_commandList, framebuffer, m_renderTargets->HdrColor, &m_bindingCache);
        
        m_commandList->close();
        GetDevice()->executeCommandList(m_commandList);

        if (!m_args.saveFrameFileName.empty() && m_renderFrameIndex == m_args.saveFrameIndex)
        {
            nvrhi::ITexture* backBuffer = framebuffer->getDesc().colorAttachments[0].texture;
            bool success = SaveTexture(GetDevice(), backBuffer, m_args.saveFrameFileName.c_str());
            g_ExitCode = success ? 0 : 1;
            glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), 1);
        }

        // Swap the even and odd frame buffers
        m_renderPass->NextFrame();
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
    std::unique_ptr<PrepareLightsPass> m_prepareLightsPass;
    std::unique_ptr<RenderPass> m_renderPass;
    std::unique_ptr<RtxdiResources> m_rtxdiResources;

    UIData& m_ui;
    CommandLineArguments m_args;
    bool m_cameraInitialized = false;
    uint32_t m_renderFrameIndex = 0;
};

void ProcessCommandLine(int argc, char** argv, app::DeviceCreationParameters& deviceParams, CommandLineArguments& args)
{
    using namespace cxxopts;

    Options options("MinimalSample", "RTXDI Minimal Sample");

    bool useVk = false;
    options.add_options()
        ("debug", "Enable debug runtime", value<bool>())
        ("vk", "Use Vulkan", value(useVk))
        ("save-file", "Save frame to file and exit", value(args.saveFrameFileName))
        ("save-frame", "Index of the frame to save (default 0)", value(args.saveFrameIndex))
        ("scene", "Scene file path (VFS path, e.g. /Assets/Media/Arcade/Arcade.gltf)", value(args.scenePath))
        ;

    auto result = options.parse(argc, argv);

    if (result.count("debug"))
    {
        deviceParams.enableDebugRuntime = true;
        deviceParams.enableNvrhiValidationLayer = true;
    }

#if DONUT_WITH_DX12 && DONUT_WITH_VULKAN
    args.graphicsApi = useVk ? nvrhi::GraphicsAPI::VULKAN : nvrhi::GraphicsAPI::D3D12;
#elif DONUT_WITH_DX12
    args.graphicsApi = nvrhi::GraphicsAPI::D3D12;
#elif DONUT_WITH_VULKAN
    args.graphicsApi = nvrhi::GraphicsAPI::VULKAN;
#endif

    if (args.saveFrameIndex != 0 && args.saveFrameFileName.empty())
    {
        log::warning("--save-frame is set but --save-file is not given. It will be ignored.");
    }
}

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

#if defined(_WIN32)
    ProcessCommandLine(__argc, __argv, deviceParams, args);
#else
    ProcessCommandLine(argc, argv, deviceParams, args);
#endif

    app::DeviceManager* deviceManager = app::DeviceManager::Create(args.graphicsApi);
    
    const char* apiString = nvrhi::utils::GraphicsAPIToString(deviceManager->GetGraphicsAPI());

    std::string windowTitle = "Hello RTXDI (" + std::string(apiString) + ")";
    
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
