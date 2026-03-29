/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "ImageComparison.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>

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
