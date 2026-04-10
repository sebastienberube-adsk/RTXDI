/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/


#include "Testing.h"
#include "UserInterface.h"

#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>

#include <cxxopts.hpp>
#include <filesystem>

using namespace donut;
namespace fs = std::filesystem;

const char* g_ApplicationTitle = "RTXDI Intermediate Sample";

static void toupper(std::string& s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::toupper(c); });
}

std::istream& operator>> (std::istream& is, AntiAliasingMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "OFF")
        mode = AntiAliasingMode::None;
    else if (s == "ACC")
        mode = AntiAliasingMode::Accumulation;
    else if (s == "TAA")
        mode = AntiAliasingMode::TAA;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --aa-mode argument.");
    
    return is;
}

std::istream& operator>> (std::istream& is, DirectLightingMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "NONE")
        mode = DirectLightingMode::None;
    else if (s == "BRDF")
        mode = DirectLightingMode::Brdf;
    else if (s == "RESTIR")
        mode = DirectLightingMode::ReStir;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --direct-mode argument.");

    return is;
}

std::istream& operator>> (std::istream& is, IndirectLightingMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "NONE")
        mode = IndirectLightingMode::None;
    else if (s == "BRDF")
        mode = IndirectLightingMode::Brdf;
    else if (s == "RESTIRGI")
        mode = IndirectLightingMode::ReStirGI;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --indirect-mode argument.");

    return is;
}

namespace rtxdi
{
std::istream& operator>> (std::istream& is, ReSTIRDI_ResamplingMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "NONE")
        mode = rtxdi::ReSTIRDI_ResamplingMode::None;
    else if (s == "TEMPORAL")
        mode = rtxdi::ReSTIRDI_ResamplingMode::Temporal;
    else if (s == "SPATIAL")
        mode = rtxdi::ReSTIRDI_ResamplingMode::Spatial;
    else if (s == "TEMPORAL_SPATIAL")
        mode = rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial;
    else if (s == "FUSED")
        mode = rtxdi::ReSTIRDI_ResamplingMode::FusedSpatiotemporal;

    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --gi-mode argument.");

    return is;
}

std::istream& operator>> (std::istream& is, ReSTIRGI_ResamplingMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "NONE")
        mode = rtxdi::ReSTIRGI_ResamplingMode::None;
    else if (s == "TEMPORAL")
        mode = rtxdi::ReSTIRGI_ResamplingMode::Temporal;
    else if (s == "SPATIAL")
        mode = rtxdi::ReSTIRGI_ResamplingMode::Spatial;
    else if (s == "TEMPORAL_SPATIAL")
        mode = rtxdi::ReSTIRGI_ResamplingMode::TemporalAndSpatial;
    else if (s == "FUSED")
        mode = rtxdi::ReSTIRGI_ResamplingMode::FusedSpatiotemporal;

    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --gi-mode argument.");

    return is;
}
}

// A hacky operator to allow selecting the preset
std::istream& operator>> (std::istream& is, UIData& ui)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "FAST")
        ui.preset = QualityPreset::Fast;
    else if (s == "MEDIUM")
        ui.preset = QualityPreset::Medium;
    else if (s == "UNBIASED")
        ui.preset = QualityPreset::Unbiased;
    else if (s == "ULTRA")
        ui.preset = QualityPreset::Ultra;
    else if (s == "REFERENCE")
        ui.preset = QualityPreset::Reference;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --preset argument.");

    ui.ApplyPreset();

    return is;
}

void ProcessCommandLine(int argc, char** argv, donut::app::DeviceCreationParameters& deviceParams, UIData& ui, CommandLineArguments& args)
{
    using namespace cxxopts;
    
    Options options(argv[0], g_ApplicationTitle);

    bool help = false;
    bool useVk = false;
    ibool checkerboard = false;
    bool minimalSampleCompat = false;
    options.add_options()
        ("aa-mode", "Anti-aliasing mode: OFF, ACC, TAA", value(ui.aaMode))
        ("alpha-tested", "Alpha-tested materials toggle", value(ui.gbufferSettings.enableAlphaTestedGeometry))
        ("animation", "Animations toggle", value(ui.enableAnimations))
        ("benchmark", "Run the benchmark", value(args.benchmark))
        ("bloom", "Bloom effect toggle", value(ui.enableBloom))
        ("checkerboard", "Use checkerboard rendering", value(checkerboard))
        ("d,debug", "Enable the DX12 or Vulkan validation layers", value(deviceParams.enableDebugRuntime))
        ("disable-bg-opt", "Disable DX12 driver background optimization", value(args.disableBackgroundOptimization))
        ("direct-resampling", "Direct lighting resampling mode: NONE, TEMPORAL, SPATIAL, TEMPORAL_SPATIAL, FUSED", value(ui.restirDI.resamplingMode))
        ("fullscreen", "Run in full screen", value(deviceParams.startFullscreen))
        ("h,help", "Display this help message", value(help))
        ("height", "Window height", value(deviceParams.backBufferHeight))
        ("indirect-resampling", "ReSTIR GI resampling mode: NONE, TEMPORAL, SPATIAL, TEMPORAL_SPATIAL, FUSED", value(ui.restirGI.resamplingMode))
        ("noise-mix", "Amount of noise to mix in", value(ui.noiseMix))
        ("pixel-jitter", "Pixel jitter toggle", value(ui.enablePixelJitter))
        ("preset", "Rendering settings preset: FAST, MEDIUM, UNBIASED, ULTRA, REFERENCE", value(ui))
        ("rasterize-gbuffer", "G-buffer rasterization toggle", value(ui.rasterizeGBuffer))
        ("ray-query", "Ray Query toggle", value(ui.useRayQuery))
        ("direct-mode", "Direct lighting mode: NONE, BRDF, RESTIR", value(ui.directLightingMode))
        ("indirect-mode", "Indirect lighting mode: NONE, BRDF, RESTIRGI", value(ui.indirectLightingMode))
        ("render-width", "Internal render target width, overrides window size", value(args.renderWidth))
        ("render-height", "Internal render target height, overrides window size", value(args.renderHeight))
        ("save-file", "Save frame to file and exit", value(args.saveFrameFileName))
        ("save-frame", "Index of the frame to save, default is 0", value(args.saveFrameIndex))
        ("scene", "Scene file path (VFS path, e.g. /Assets/Media/arcade.scene.json or /Assets/Media/Arcade/Arcade.gltf)", value(args.scenePath))
        ("basic-tonemap", "Basic tone mapping in compositing (same as MinimalSample)", value(ui.enableBasicToneMapping))
        ("environment", "Environment map rendering toggle", value(ui.enableEnvironmentRendering))
        ("minimal-sample-compatibility-mode", "Match MinimalGISample parameter defaults", value(minimalSampleCompat))
        ("tone-mapping", "Tone mapping toggle", value(ui.enableToneMapping))
        ("transparent", "Transparent materials toggle", value(ui.gbufferSettings.enableTransparentGeometry))
        ("verbose", "Enable debug log messages", value(args.verbose))
        ("vk", "Run the application using Vulkan (otherwise D3D12 if supported)", value(useVk))
        ("width", "Window width", value(deviceParams.backBufferWidth))
    ;

    try
    {
        options.parse(argc, argv);

        if (help)
        {
#if defined(_WIN32) && !defined(IS_CONSOLE_APP)
            MessageBoxA(nullptr, options.help().c_str(), g_ApplicationTitle, MB_ICONINFORMATION);
#else
            printf("%s", options.help().c_str());
#endif
            exit(0);
        }
        
    }
    catch (const std::exception& e)
    {
        donut::log::error("%s", e.what());
        exit(1);
    }

    if (args.saveFrameIndex != 0 && args.saveFrameFileName.empty())
    {
        log::warning("The --save-frame argument is used without --save-file. It will be ignored.");
    }

#if DONUT_WITH_DX12 && DONUT_WITH_VULKAN
    args.graphicsApi = useVk ? nvrhi::GraphicsAPI::VULKAN : nvrhi::GraphicsAPI::D3D12;
#elif DONUT_WITH_DX12
    args.graphicsApi = nvrhi::GraphicsAPI::D3D12;
#elif DONUT_WITH_VULKAN
    args.graphicsApi = nvrhi::GraphicsAPI::VULKAN;
#else
#error "At least one of USE_DX12 and USE_VK macros needs to be defined"
#endif
    
    deviceParams.enableNvrhiValidationLayer = deviceParams.enableDebugRuntime;

    if (args.benchmark)
        ui.animationFrame = 0;

    if (checkerboard)
        ui.restirDIStaticParams.CheckerboardSamplingMode = rtxdi::CheckerboardMode::Black;

    if (minimalSampleCompat)
    {
        // Match MinimalGISample's resampling mode and general app settings
        ui.restirDI.resamplingMode = rtxdi::ReSTIRDI_ResamplingMode::FusedSpatiotemporal;
        ui.enableBasicToneMapping = true;
        ui.enableToneMapping = false;
        ui.enableBloom = false;
        ui.rasterizeGBuffer = false;
        ui.gbufferSettings.enableAlphaTestedGeometry = false;
        ui.gbufferSettings.enableTransparentGeometry = false;
        ui.aaMode = AntiAliasingMode::None;
        args.disableEnvironment = true;

        // Initial sampling — match MinimalGISample LightingPasses::Settings + SDK defaults
        ui.restirDI.initialSamplingParams.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::Uniform;
        ui.restirDI.initialSamplingParams.numPrimaryLocalLightSamples = 8;
        ui.restirDI.initialSamplingParams.numPrimaryBrdfSamples = 1;
        ui.restirDI.initialSamplingParams.numPrimaryInfiniteLightSamples = 0;
        ui.restirDI.initialSamplingParams.numPrimaryEnvironmentSamples = 0;
        ui.restirDI.initialSamplingParams.enableInitialVisibility = true;
        ui.restirDI.initialSamplingParams.brdfCutoff = 0.0f;

        // Temporal resampling — match MinimalGISample overrides + SDK defaults
        ui.restirDI.temporalResamplingParams.temporalBiasCorrection = ReSTIRDI_TemporalBiasCorrectionMode::Basic;
        ui.restirDI.temporalResamplingParams.discardInvisibleSamples = true;
        ui.restirDI.temporalResamplingParams.enablePermutationSampling = true;
        ui.restirDI.temporalResamplingParams.permutationSamplingThreshold = 0.9f;
        ui.restirDI.temporalResamplingParams.maxHistoryLength = 20;
        ui.restirDI.temporalResamplingParams.temporalDepthThreshold = 0.1f;
        ui.restirDI.temporalResamplingParams.temporalNormalThreshold = 0.5f;
        ui.restirDI.temporalResamplingParams.enableBoilingFilter = true;
        ui.restirDI.temporalResamplingParams.boilingFilterStrength = 0.2f;

        // Spatial resampling — match MinimalGISample overrides + SDK defaults
        ui.restirDI.spatialResamplingParams.numSpatialSamples = 1;
        ui.restirDI.spatialResamplingParams.numDisocclusionBoostSamples = 0;
        ui.restirDI.spatialResamplingParams.spatialSamplingRadius = 32.0f;
        ui.restirDI.spatialResamplingParams.spatialBiasCorrection = ReSTIRDI_SpatialBiasCorrectionMode::Basic;
        ui.restirDI.spatialResamplingParams.discountNaiveSamples = 0;

        // MinimalGISample compatibility: disable material similarity filtering.
        ui.lightingSettings.enableMaterialSimilarityTest = false;

        // Shading — match MinimalGISample (which uses fresh visibility checks, not reuse)
        ui.restirDI.shadingParams.enableFinalVisibility = true;
        ui.restirDI.shadingParams.reuseFinalVisibility = false;
        ui.restirDI.shadingParams.finalVisibilityMaxAge = 4;
        ui.restirDI.shadingParams.finalVisibilityMaxDistance = 16.0f;
    }
}

void ApplicationLogCallback(log::Severity severity, const char* message)
{
    static std::mutex g_LogMutex;

    const char* severityText = "";
    switch (severity)
    {
    case log::Severity::Debug: severityText = "DEBUG";  break;
    case log::Severity::Info: severityText = "INFO";  break;
    case log::Severity::Warning: severityText = "WARNING"; break;
    case log::Severity::Error: severityText = "ERROR"; break;
    case log::Severity::Fatal: severityText = "FATAL ERROR"; break;
    default:
        break;
    }

    {
        std::lock_guard<std::mutex> lockGuard(g_LogMutex);

#if defined(_WIN32) && !defined(IS_CONSOLE_APP)
        static char buf[4096];
        snprintf(buf, std::size(buf), "%s: %s", severityText, message);

        OutputDebugStringA(buf);
        OutputDebugStringA("\n");

        if (severity >= log::Severity::Error)
        {
            MessageBoxA(nullptr, buf, g_ApplicationTitle, MB_ICONERROR);
        }
#else
        FILE* fd = (severity >= log::Severity::Error) ? stderr : stdout;

        fprintf(fd, "%s: %s\n", severityText, message);
        fflush(fd);
#endif
    }

    if (severity == log::Severity::Fatal)
        abort();
}

