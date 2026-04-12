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
#include <iomanip>
#include <sstream>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static float SafeRatio(float value, float thresh)
{
    return thresh > 0.0f ? value / thresh : (value > 0.0f ? 1e6f : 0.0f);
}

// ---------------------------------------------------------------------------
// GetFailureDegree
// ---------------------------------------------------------------------------

float GetFailureDegree(const TileResult& tile, const StochasticThresholds& thresholds)
{
    float worst = 0.0f;
    for (int c = 0; c < 3; ++c)
    {
        if (!tile.channels[c].passed)
        {
            worst = std::max(worst, SafeRatio(tile.channels[c].absAvgDelta,
                                              thresholds.absAvgDeltaThreshold));
            worst = std::max(worst, SafeRatio(tile.channels[c].relDelta,
                                              thresholds.relDifferenceThreshold));
            worst = std::max(worst, SafeRatio(tile.channels[c].absStdDevDelta,
                                              thresholds.absStdDevDeltaThreshold));
        }
    }
    return worst;
}

// ---------------------------------------------------------------------------
// CompareStochastic -- pure computation, no summary generation
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
    result.imageWidth = static_cast<uint32_t>(width);
    result.imageHeight = static_cast<uint32_t>(height);
    result.tiles.resize(static_cast<size_t>(result.tilesX) * result.tilesY);
    result.passed = true;
    result.imagePassed = true;
    result.failingTileCount = 0;

    const bool stdDevEnabled = (thresholds.absStdDevDeltaThreshold > 0.0f);
    double imageSumA[3] = { 0, 0, 0 };
    double imageSumB[3] = { 0, 0, 0 };

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
                        imageSumA[c] += vA;
                        imageSumB[c] += vB;
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

    const double totalPixels = static_cast<double>(width * height);
    for (int c = 0; c < 3; ++c)
    {
        float avgA = static_cast<float>(imageSumA[c] / totalPixels);
        float avgB = static_cast<float>(imageSumB[c] / totalPixels);
        float absAvg = std::abs(avgA - avgB);
        float maxAvg = std::max(avgA, avgB);
        float rel = (maxAvg > 1e-7f) ? (absAvg / maxAvg) : 0.0f;

        result.imageAvgA[c] = avgA;
        result.imageAvgB[c] = avgB;
        result.imageAbsAvgDelta[c] = absAvg;
        result.imageRelDelta[c] = rel;

        bool chPass = (absAvg <= thresholds.imageAbsAvgDeltaThreshold) ||
                      (rel <= thresholds.imageRelDifferenceThreshold);
        result.imageChannelPassed[c] = chPass;
        if (!chPass)
            result.imagePassed = false;
    }

    if (!result.imagePassed)
        result.passed = false;

    return result;
}

// ---------------------------------------------------------------------------
// FormatStochasticSummary
// ---------------------------------------------------------------------------

std::string FormatStochasticSummary(
    const StochasticResult& result,
    const StochasticThresholds& thresholds)
{
    const bool stdDevEnabled = (thresholds.absStdDevDeltaThreshold > 0.0f);

    struct { float value = 0.0f; uint32_t tileX = 0, tileY = 0; } worstAbsAvg, worstRel, worstSD;
    for (const auto& t : result.tiles)
    {
        for (int c = 0; c < 3; ++c)
        {
            if (t.channels[c].absAvgDelta > worstAbsAvg.value)
                worstAbsAvg = { t.channels[c].absAvgDelta, t.tileX, t.tileY };
            if (t.channels[c].relDelta > worstRel.value)
                worstRel = { t.channels[c].relDelta, t.tileX, t.tileY };
            if (t.channels[c].absStdDevDelta > worstSD.value)
                worstSD = { t.channels[c].absStdDevDelta, t.tileX, t.tileY };
        }
    }

    std::stringstream ss;
    ss << std::defaultfloat << std::setprecision(6);
    ss << "Stochastic comparison: " << result.tilesX << "x" << result.tilesY
       << " tiles (" << thresholds.tileSize << "px), "
       << result.imageWidth << "x" << result.imageHeight << " image\n";
    ss << "  Thresholds: absAvgDelta <= " << thresholds.absAvgDeltaThreshold
       << " OR relDiff <= " << (thresholds.relDifferenceThreshold * 100.0f) << "%";
    if (stdDevEnabled)
        ss << ", absStdDevDelta <= " << thresholds.absStdDevDeltaThreshold;
    ss << "\n";
    ss << "              imageAbsAvgDelta <= " << thresholds.imageAbsAvgDeltaThreshold
       << " OR imageRelDiff <= " << (thresholds.imageRelDifferenceThreshold * 100.0f) << "%\n";

    ss << std::fixed << std::setprecision(3);
    ss << "     Max absolute avg delta   "
       << std::setw(8) << worstAbsAvg.value
       << " [" << std::setprecision(2) << SafeRatio(worstAbsAvg.value, thresholds.absAvgDeltaThreshold)
       << "x threshold=" << std::setprecision(3) << thresholds.absAvgDeltaThreshold
       << ", tile(" << worstAbsAvg.tileX << "," << worstAbsAvg.tileY << ")]\n";
    ss << "     Max relative difference  " << std::setprecision(2)
       << std::setw(7) << (worstRel.value * 100.0f) << "%"
       << " [" << SafeRatio(worstRel.value, thresholds.relDifferenceThreshold)
       << "x threshold=" << (thresholds.relDifferenceThreshold * 100.0f) << "%"
       << ", tile(" << worstRel.tileX << "," << worstRel.tileY << ")]\n";
    ss << std::setprecision(3);
    ss << "     Max abs stddev delta     "
       << std::setw(8) << worstSD.value
       << " [" << std::setprecision(2) << SafeRatio(worstSD.value, thresholds.absStdDevDeltaThreshold)
       << "x threshold=" << std::setprecision(3) << thresholds.absStdDevDeltaThreshold
       << ", tile(" << worstSD.tileX << "," << worstSD.tileY << ")]\n";

    ss << std::setprecision(6) << std::defaultfloat;
    for (int c = 0; c < 3; ++c)
    {
        const char channelName = (c == 0) ? 'R' : (c == 1) ? 'G' : 'B';
        ss << "     Image avg " << channelName
           << ": avgA=" << result.imageAvgA[c]
           << " avgB=" << result.imageAvgB[c]
           << " abs=" << result.imageAbsAvgDelta[c]
           << " rel=" << (result.imageRelDelta[c] * 100.0f) << "%"
           << " [" << (result.imageChannelPassed[c] ? "PASS" : "FAIL") << "]\n";
    }

    ss << "  Result: " << (result.passed ? "PASSED" : "FAILED")
       << " (" << result.failingTileCount << " / "
       << (result.tilesX * result.tilesY) << " tiles failed)\n";

    if (!result.imagePassed)
        ss << "  Image-average result: FAILED (at least one RGB channel exceeded both image thresholds)\n";

    if (!result.passed)
    {
        std::vector<const TileResult*> failed;
        failed.reserve(result.failingTileCount);
        for (const auto& t : result.tiles)
        {
            if (!t.passed)
                failed.push_back(&t);
        }
        std::sort(failed.begin(), failed.end(),
            [&thresholds](const TileResult* a, const TileResult* b) {
            return GetFailureDegree(*a, thresholds) > GetFailureDegree(*b, thresholds);
        });

        ss << std::setprecision(6) << std::defaultfloat;
        int shown = 0;
        for (const auto* t : failed)
        {
            float abs = 0, rel = 0, dSD = 0, avgA = 0, avgB = 0, sdA = 0, sdB = 0;
            for (int c = 0; c < 3; ++c)
            {
                if (t->channels[c].absAvgDelta > abs)
                {
                    abs  = t->channels[c].absAvgDelta;
                    rel  = t->channels[c].relDelta;
                    avgA = t->channels[c].avgA;
                    avgB = t->channels[c].avgB;
                }
                if (t->channels[c].absStdDevDelta > dSD)
                {
                    dSD = t->channels[c].absStdDevDelta;
                    sdA = t->channels[c].stdDevA;
                    sdB = t->channels[c].stdDevB;
                }
            }
            ss << "    FAIL tile(" << t->tileX << "," << t->tileY << "):"
               << " abs=" << abs
               << " rel=" << (rel * 100.0f) << "%"
               << " |dSD|=" << dSD
               << " avgA=" << avgA
               << " avgB=" << avgB
               << " sdA=" << sdA
               << " sdB=" << sdB
               << "\n";
            if (++shown >= 10)
            {
                ss << "    ... (" << (result.failingTileCount - shown) << " more)\n";
                break;
            }
        }
    }

    return ss.str();
}
