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

#include <string>
#include <vector>
#include <cstdint>

// ---------------------------------------------------------------------------
// Tile-based stochastic image comparison (replicated from Aurora test infra).
// ---------------------------------------------------------------------------

struct StochasticThresholds
{
    uint32_t tileSize = 32;
    float absAvgDeltaThreshold = 0.05f;
    float relDifferenceThreshold = 0.08f;
    float absStdDevDeltaThreshold = 0.05f;
};

struct TileChannelStats
{
    float avgA = 0.0f;
    float avgB = 0.0f;
    float absAvgDelta = 0.0f;
    float relDelta = 0.0f;
    float stdDevA = 0.0f;
    float stdDevB = 0.0f;
    float absStdDevDelta = 0.0f;
    bool passed = true;
};

struct TileResult
{
    uint32_t tileX = 0;
    uint32_t tileY = 0;
    TileChannelStats channels[3];
    bool passed = true;
};

struct StochasticResult
{
    bool passed = true;
    float maxAbsAvgDelta[3] = { 0.0f, 0.0f, 0.0f };
    float maxRelDelta[3] = { 0.0f, 0.0f, 0.0f };
    float maxAbsStdDevDelta[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t tilesX = 0;
    uint32_t tilesY = 0;
    std::vector<TileResult> tiles;
    uint32_t failingTileCount = 0;
    std::string summary;
};

StochasticResult CompareStochastic(
    const uint8_t* pixelsA,
    const uint8_t* pixelsB,
    size_t width,
    size_t height,
    const StochasticThresholds& thresholds = StochasticThresholds());

// LoadImageRGBA8 uses stb_image; the implementation is in ImageIO.cpp
// which must be compiled alongside a STB_IMAGE_IMPLEMENTATION provider.
std::vector<uint8_t> LoadImageRGBA8(const std::string& path, size_t& width, size_t& height);
