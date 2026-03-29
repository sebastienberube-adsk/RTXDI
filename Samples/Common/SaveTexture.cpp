/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "SaveTexture.h"

#include <donut/core/log.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using namespace donut;

bool SaveTexture(nvrhi::IDevice* device, nvrhi::ITexture* texture, const char* writeFileName)
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
