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

#include <nvrhi/nvrhi.h>
#include <string>
#include <vector>
#include <cstdint>

#include <ImageComparison.h>
#include <SaveTexture.h>

namespace donut::app {
    struct DeviceCreationParameters;
}

struct UIData;

struct CommandLineArguments
{
    nvrhi::GraphicsAPI graphicsApi = nvrhi::GraphicsAPI::D3D12;
    uint32_t saveFrameIndex = 64;
    std::string saveFrameFileName;
    std::string compareBaselinePath;
    std::string scenePath;
    int renderWidth = 0;
    int renderHeight = 0;
    int diagMode = 0;
};

void ProcessCommandLine(int argc, char** argv,
    donut::app::DeviceCreationParameters& deviceParams,
    UIData& ui,
    CommandLineArguments& args);
