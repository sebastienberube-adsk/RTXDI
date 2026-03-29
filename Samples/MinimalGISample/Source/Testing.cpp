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
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>

using namespace donut;
namespace fs = std::filesystem;

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

// ---------------------------------------------------------------------------
// GPU texture readback and save to BMP via stb_image_write
// ---------------------------------------------------------------------------

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

    // Copy to tightly-packed buffer (rowPitch may be larger than width * 4).
    std::vector<uint8_t> packedPixels(desc.width * desc.height * 4);
    for (uint32_t row = 0; row < desc.height; row++)
    {
        memcpy(
            packedPixels.data() + row * desc.width * 4,
            static_cast<const char*>(pData) + row * rowPitch,
            desc.width * 4);
    }

    device->unmapStagingTexture(stagingTexture);

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

// ---------------------------------------------------------------------------
// Load an RGBA8 image from disk via stb_image
// ---------------------------------------------------------------------------

std::vector<uint8_t> LoadImageRGBA8(const std::string& path, size_t& width, size_t& height)
{
    int w = 0, h = 0, channels = 0;
    uint8_t* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data)
    {
        std::cerr << "LoadImageRGBA8: failed to load " << path << std::endl;
        return {};
    }

    width = static_cast<size_t>(w);
    height = static_cast<size_t>(h);

    std::vector<uint8_t> pixels(data, data + w * h * 4);
    stbi_image_free(data);
    return pixels;
}

// ---------------------------------------------------------------------------
// Tile-based stochastic image comparison
// ---------------------------------------------------------------------------

StochasticResult CompareStochastic(
    const uint8_t* pixelsA,
    const uint8_t* pixelsB,
    size_t width,
    size_t height,
    const StochasticThresholds& thresholds)
{
    StochasticResult result;
    const uint32_t ts = thresholds.tileSize;
    result.tilesX = static_cast<uint32_t>((width + ts - 1) / ts);
    result.tilesY = static_cast<uint32_t>((height + ts - 1) / ts);
    result.tiles.resize(static_cast<size_t>(result.tilesX) * result.tilesY);
    result.passed = true;
    result.failingTileCount = 0;

    const bool stdDevEnabled = (thresholds.absStdDevDeltaThreshold > 0.0f);

    for (uint32_t ty = 0; ty < result.tilesY; ++ty)
    {
        for (uint32_t tx = 0; tx < result.tilesX; ++tx)
        {
            TileResult& tile = result.tiles[ty * result.tilesX + tx];
            tile.tileX = tx;
            tile.tileY = ty;
            tile.passed = true;

            uint32_t x0 = tx * ts;
            uint32_t y0 = ty * ts;
            uint32_t x1 = std::min(x0 + ts, static_cast<uint32_t>(width));
            uint32_t y1 = std::min(y0 + ts, static_cast<uint32_t>(height));
            uint32_t pixelCount = (x1 - x0) * (y1 - y0);

            double sumA[3] = { 0, 0, 0 };
            double sumB[3] = { 0, 0, 0 };
            double sumSqA[3] = { 0, 0, 0 };
            double sumSqB[3] = { 0, 0, 0 };

            for (uint32_t y = y0; y < y1; ++y)
            {
                for (uint32_t x = x0; x < x1; ++x)
                {
                    size_t idx = (y * width + x) * 4;
                    for (int c = 0; c < 3; ++c)
                    {
                        double vA = pixelsA[idx + c] / 255.0;
                        double vB = pixelsB[idx + c] / 255.0;
                        sumA[c] += vA;
                        sumB[c] += vB;
                        sumSqA[c] += vA * vA;
                        sumSqB[c] += vB * vB;
                    }
                }
            }

            for (int c = 0; c < 3; ++c)
            {
                double n = static_cast<double>(pixelCount);
                float avgA = static_cast<float>(sumA[c] / n);
                float avgB = static_cast<float>(sumB[c] / n);

                double varA = std::max(0.0, sumSqA[c] / n - (sumA[c] / n) * (sumA[c] / n));
                double varB = std::max(0.0, sumSqB[c] / n - (sumB[c] / n) * (sumB[c] / n));
                float sdA = static_cast<float>(std::sqrt(varA));
                float sdB = static_cast<float>(std::sqrt(varB));

                float absAvg = std::abs(avgA - avgB);
                float maxAvg = std::max(avgA, avgB);
                float rel = (maxAvg > 1e-7f) ? (absAvg / maxAvg) : 0.0f;
                float absSD = std::abs(sdA - sdB);

                tile.channels[c].avgA = avgA;
                tile.channels[c].avgB = avgB;
                tile.channels[c].absAvgDelta = absAvg;
                tile.channels[c].relDelta = rel;
                tile.channels[c].stdDevA = sdA;
                tile.channels[c].stdDevB = sdB;
                tile.channels[c].absStdDevDelta = absSD;

                bool chPass = (absAvg <= thresholds.absAvgDeltaThreshold) ||
                              (rel <= thresholds.relDifferenceThreshold);

                if (stdDevEnabled && absSD > thresholds.absStdDevDeltaThreshold)
                    chPass = false;

                tile.channels[c].passed = chPass;

                if (!chPass)
                    tile.passed = false;

                if (absAvg > result.maxAbsAvgDelta[c])
                    result.maxAbsAvgDelta[c] = absAvg;
                if (rel > result.maxRelDelta[c])
                    result.maxRelDelta[c] = rel;
                if (absSD > result.maxAbsStdDevDelta[c])
                    result.maxAbsStdDevDelta[c] = absSD;
            }

            if (!tile.passed)
            {
                result.passed = false;
                result.failingTileCount++;
            }
        }
    }

    // Build summary string.
    std::stringstream ss;
    ss.precision(6);
    ss << "Stochastic comparison: " << result.tilesX << "x" << result.tilesY
       << " tiles (" << thresholds.tileSize << "px), "
       << width << "x" << height << " image\n";
    ss << "  Thresholds: absAvgDelta <= " << thresholds.absAvgDeltaThreshold
       << " OR relDiff <= " << (thresholds.relDifferenceThreshold * 100.0f) << "%";
    if (stdDevEnabled)
        ss << ", absStdDevDelta <= " << thresholds.absStdDevDeltaThreshold;
    ss << "\n";
    ss << "  Max absolute avg delta    R=" << result.maxAbsAvgDelta[0]
       << "  G=" << result.maxAbsAvgDelta[1]
       << "  B=" << result.maxAbsAvgDelta[2] << "\n";
    ss << "  Max relative difference   R=" << (result.maxRelDelta[0] * 100.0f) << "%"
       << "  G=" << (result.maxRelDelta[1] * 100.0f) << "%"
       << "  B=" << (result.maxRelDelta[2] * 100.0f) << "%\n";
    ss << "  Max abs stddev delta      R=" << result.maxAbsStdDevDelta[0]
       << "  G=" << result.maxAbsStdDevDelta[1]
       << "  B=" << result.maxAbsStdDevDelta[2] << "\n";
    ss << "  Result: " << (result.passed ? "PASSED" : "FAILED")
       << " (" << result.failingTileCount << " / "
       << (result.tilesX * result.tilesY) << " tiles failed)\n";

    if (!result.passed)
    {
        int shown = 0;
        for (const auto& t : result.tiles)
        {
            if (!t.passed)
            {
                ss << "    FAIL tile(" << t.tileX << "," << t.tileY << "):";
                const char* chName[] = { "R", "G", "B" };
                for (int c = 0; c < 3; ++c)
                {
                    if (!t.channels[c].passed)
                    {
                        ss << " " << chName[c]
                           << "(abs=" << t.channels[c].absAvgDelta
                           << " rel=" << (t.channels[c].relDelta * 100.0f) << "%"
                           << " avgA=" << t.channels[c].avgA
                           << " avgB=" << t.channels[c].avgB
                           << " sdA=" << t.channels[c].stdDevA
                           << " sdB=" << t.channels[c].stdDevB
                           << " |dSD|=" << t.channels[c].absStdDevDelta << ")";
                    }
                }
                ss << "\n";
                if (++shown >= 10)
                {
                    ss << "    ... (" << (result.failingTileCount - shown) << " more)\n";
                    break;
                }
            }
        }
    }

    result.summary = ss.str();
    return result;
}
