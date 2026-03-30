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

#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>

#include <cxxopts.hpp>

#include <filesystem>
#include <iostream>

using namespace donut;
namespace fs = std::filesystem;

std::istream& operator>> (std::istream& is, AntiAliasingMode& mode)
{
    std::string token;
    is >> token;
    if (token == "OFF" || token == "off" || token == "NONE" || token == "none")
        mode = AntiAliasingMode::None;
    else if (token == "ACC" || token == "acc" || token == "ACCUMULATION" || token == "accumulation")
        mode = AntiAliasingMode::Accumulation;
    else
        throw cxxopts::exceptions::parsing("Invalid aa-mode value: " + token);
    return is;
}

// ---------------------------------------------------------------------------
// Command-line processing
// ---------------------------------------------------------------------------

void ProcessCommandLine(int argc, char** argv,
    donut::app::DeviceCreationParameters& deviceParams,
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
        ("disable-gi", "Disable ReSTIR GI (DI only)", value<bool>())
        ("tone-mapping", "Enable basic tone mapping (default: 1)", value(args.enableToneMapping))
        ("aa-mode", "Anti-aliasing mode: OFF or ACC (default: OFF)", value(args.aaMode))
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


