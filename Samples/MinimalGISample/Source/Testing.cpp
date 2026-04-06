/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
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

static void toupper(std::string& s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::toupper(c); });
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
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --direct-resampling argument.");

    return is;
}
}

// ---------------------------------------------------------------------------
// Command-line processing
// ---------------------------------------------------------------------------

void ProcessCommandLine(int argc, char** argv,
    donut::app::DeviceCreationParameters& deviceParams,
    UIData& ui,
    CommandLineArguments& args)
{
    using namespace cxxopts;

    Options options("MinimalGISample", "RTXDI Minimal GI Sample");

    options.add_options()
        ("debug", "Enable debug runtime", value<bool>())
        ("vk", "Use Vulkan", value<bool>())
        ("save-file", "Save frame to file and exit", value(args.saveFrameFileName))
        ("save-frame", "Index of the frame to save (default 64)", value(args.saveFrameIndex))
        ("compare-baseline", "Path to baseline image for stochastic comparison", value(args.compareBaselinePath))
        ("scene", "Scene file path (VFS path, e.g. /Assets/Media/Arcade/Arcade.gltf)", value(args.scenePath))
        ("width", "Render width override", value(args.renderWidth))
        ("height", "Render height override", value(args.renderHeight))
        ("direct-resampling", "Direct lighting resampling mode: NONE, TEMPORAL, SPATIAL, TEMPORAL_SPATIAL, FUSED", value(ui.lightingSettings.resamplingMode))
        ("disable-gi", "Disable ReSTIR GI (DI only)", value<bool>())
        ;

    auto result = options.parse(argc, argv);

    if (result.count("debug"))
    {
        deviceParams.enableDebugRuntime = true;
        deviceParams.enableNvrhiValidationLayer = true;
    }

    if (result.count("vk"))
    {
        args.graphicsApi = nvrhi::GraphicsAPI::VULKAN;
    }

    if (result.count("disable-gi"))
    {
        args.disableGI = true;
    }

    if (args.renderWidth > 0 && args.renderHeight > 0)
    {
        deviceParams.backBufferWidth = args.renderWidth;
        deviceParams.backBufferHeight = args.renderHeight;
    }

    if (args.saveFrameIndex != 0 && args.saveFrameFileName.empty() && args.compareBaselinePath.empty())
    {
        log::warning("--save-frame is set but neither --save-file nor --compare-baseline is given. It will be ignored.");
    }
}


