/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include <ImageComparison.h>
#include <gtest/gtest.h>
#include <stb_image_write.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

// Thresholds calibrated from MinimalSample_SelfComparison (frame 50 vs 75,
// 32px tiles): the natural stochastic variance of equivalent images rendered
// with different random seeds.  Measured maximums were:
//   absAvgDelta  = 0.0473    relDifference = 24.6%    absStdDevDelta = 0.0655
// Values below include ~15% headroom for run-to-run variation.
static StochasticThresholds GetThresholds()
{
    StochasticThresholds t;
    t.tileSize                = 32;
    t.absAvgDeltaThreshold    = 0.075f;
    t.relDifferenceThreshold  = 0.35f;
    t.absStdDevDeltaThreshold = 0.150f;
    return t;
}

static const char* kScene = "/Assets/Media/livingroom_Original.scene.json";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static fs::path GetExecutableDir()
{
#if defined(_WIN32)
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) { buf[len] = '\0'; return fs::path(buf).parent_path(); }
    return fs::current_path();
#endif
}

static fs::path GetRepoRoot()
{
    fs::path dir = GetExecutableDir();
    for (int i = 0; i < 6; ++i)
    {
        if (fs::exists(dir / "CMakeLists.txt") && fs::exists(dir / "Support"))
            return dir;
        dir = dir.parent_path();
    }
    std::cerr << "ERROR: could not locate RTXDI repo root from " << GetExecutableDir() << std::endl;
    return {};
}

static fs::path GetOutputDir()
{
    fs::path dir = GetExecutableDir() / "test_output";
    if (!fs::exists(dir))
        fs::create_directories(dir);
    return dir;
}

static std::string Quote(const fs::path& p)
{
    return "\"" + p.string() + "\"";
}

static int RunSample(const std::string& exeName, const std::string& extraArgs,
                     const fs::path& outputFile, uint32_t saveFrame)
{
    fs::path exePath = GetExecutableDir() / exeName;
#if defined(_WIN32)
    exePath.replace_extension(".exe");
#endif
    if (!fs::exists(exePath))
    {
        std::cerr << "ERROR: sample executable not found: " << exePath << std::endl;
        return 1;
    }

    std::string innerCmd = Quote(exePath)
        + " --scene " + kScene
        + " --save-file " + Quote(outputFile)
        + " --save-frame " + std::to_string(saveFrame)
        + " " + extraArgs;

#if defined(_WIN32)
    std::string cmd = "\"" + innerCmd + "\"";
#else
    std::string cmd = innerCmd;
#endif

    std::cout << "  Running: " << innerCmd << std::endl;
    int rc = std::system(cmd.c_str());
#if !defined(_WIN32)
    if (WIFEXITED(rc)) rc = WEXITSTATUS(rc);
#endif
    if (rc != 0)
        std::cerr << "  Sample exited with code " << rc << std::endl;
    return rc;
}

// Saves an annotated copy with colored 1-pixel inner borders on failed tiles.
// Border color: yellow (1x threshold) -> red (2x+ threshold).
static void SaveAnnotatedImage(const uint8_t* pixels, size_t width, size_t height,
                               const StochasticResult& result,
                               const StochasticThresholds& thresholds,
                               const fs::path& outputPath)
{
    std::vector<uint8_t> annotated(pixels, pixels + width * height * 4);
    const uint32_t ts = thresholds.tileSize;
    const uint32_t W = static_cast<uint32_t>(width);
    const uint32_t H = static_cast<uint32_t>(height);

    for (const auto& tile : result.tiles)
    {
        if (tile.passed)
            continue;

        float worstRatio = GetFailureDegree(tile, thresholds);

        // Lerp: 1x -> yellow (255,255,0), 2x+ -> red (255,0,0).
        float t = std::clamp((worstRatio - 1.0f), 0.0f, 1.0f);
        uint8_t r = 255;
        uint8_t g = static_cast<uint8_t>(255.0f * (1.0f - t) + 0.5f);
        uint8_t b = 0;

        auto setPixel = [&](uint32_t px, uint32_t py) {
            if (px < W && py < H)
            {
                size_t idx = ((size_t)py * W + px) * 4;
                annotated[idx + 0] = r;
                annotated[idx + 1] = g;
                annotated[idx + 2] = b;
                annotated[idx + 3] = 255;
            }
        };

        uint32_t x0 = tile.tileX * ts;
        uint32_t y0 = tile.tileY * ts;
        uint32_t x1 = std::min(x0 + ts, W);
        uint32_t y1 = std::min(y0 + ts, H);

        for (uint32_t x = x0; x < x1; x++)
        {
            setPixel(x, y0);
            if (y1 > y0 + 1) setPixel(x, y1 - 1);
        }
        for (uint32_t y = y0 + 1; y + 1 < y1; y++)
        {
            setPixel(x0, y);
            if (x1 > x0 + 1) setPixel(x1 - 1, y);
        }
    }

    if (stbi_write_bmp(outputPath.string().c_str(), W, H, 4, annotated.data()))
        std::cout << "  Annotated image saved to: " << outputPath << std::endl;
    else
        std::cerr << "  WARNING: failed to save annotated image to: " << outputPath << std::endl;
}

static ::testing::AssertionResult CompareImages(
    const fs::path& imageA, const fs::path& imageB, const std::string& label)
{
    size_t wA = 0, hA = 0, wB = 0, hB = 0;
    auto pixA = LoadImageRGBA8(imageA.string(), wA, hA);
    auto pixB = LoadImageRGBA8(imageB.string(), wB, hB);

    if (pixA.empty())
        return ::testing::AssertionFailure() << "Failed to load image A: " << imageA;
    if (pixB.empty())
        return ::testing::AssertionFailure() << "Failed to load image B: " << imageB;
    if (wA != wB || hA != hB)
        return ::testing::AssertionFailure()
            << "Size mismatch: " << wA << "x" << hA << " vs " << wB << "x" << hB;

    auto thresholds = GetThresholds();
    auto result = CompareStochastic(pixA.data(), pixB.data(), wA, hA, thresholds);
    std::string summary = FormatStochasticSummary(result, thresholds);
    std::cout << "  [" << label << "] " << summary;

    if (!result.passed && result.failingTileCount > 0)
    {
        std::string safeName = label;
        for (char& c : safeName)
        {
            if (c == ' ' || c == '(' || c == ')' || c == '/' || c == '\\')
                c = '_';
        }

        SaveAnnotatedImage(pixA.data(), wA, hA, result, thresholds,
            GetOutputDir() / (safeName + "_annotated.bmp"));
    }

    if (result.passed)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << summary;
}

// ---------------------------------------------------------------------------
// Test cases -- each TEST() appears individually in VS Test Explorer.
// ---------------------------------------------------------------------------

// Runs MinimalSample at two different frame counts to establish the natural
// stochastic variance of equivalent images rendered with different random
// seeds.  The thresholds in GetThresholds() are calibrated from this test.
TEST(SampleImageTests, Threshold_MinimalSample_SelfComparison)
{
    fs::path outA = GetOutputDir() / "Threshold_MinimalSample_f50.bmp";
    fs::path outB = GetOutputDir() / "Threshold_MinimalSample_f75.bmp";

    ASSERT_EQ(RunSample("MinimalSample", "", outA, 50), 0) << "MinimalSample (frame 50) failed to run";
    ASSERT_EQ(RunSample("MinimalSample", "", outB, 75), 0) << "MinimalSample (frame 75) failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "MinimalSample self frame50 vs frame75"));
}

TEST(SampleImageTests, Threshold_IntermediateSample_SelfComparison)
{
    fs::path outA = GetOutputDir() / "Threshold_IntermediateSample_compat_f64.bmp";
    fs::path outB = GetOutputDir() / "Threshold_IntermediateSample_compat_f128.bmp";

    ASSERT_EQ(RunSample("IntermediateSample", "--minimal-sample-compatibility-mode", outA, 64), 0)
        << "IntermediateSample (frame 64) failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--minimal-sample-compatibility-mode", outB, 128), 0)
        << "IntermediateSample (frame 128) failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "IntermediateSample self frame64 vs frame128"));
}

TEST(SampleImageTests, Minimal_vs_MinimalGI_DI)
{
    fs::path outA = GetOutputDir() / "Minimal_vs_MinimalGI_DI_A.bmp";
    fs::path outB = GetOutputDir() / "Minimal_vs_MinimalGI_DI_B.bmp";

    ASSERT_EQ(RunSample("MinimalSample", "", outA, 64), 0) << "MinimalSample failed to run";
    ASSERT_EQ(RunSample("MinimalGISample", "--minimal-sample-compatibility-mode", outB, 64), 0) << "MinimalGISample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "MinimalDI vs MinimalGI_DI"));
}

TEST(SampleImageTests, Minimal_vs_MinimalGI_DI_VK)
{
    fs::path outA = GetOutputDir() / "Minimal_vs_MinimalGI_DI_VK_A.bmp";
    fs::path outB = GetOutputDir() / "Minimal_vs_MinimalGI_DI_VK_B.bmp";

    ASSERT_EQ(RunSample("MinimalSample", "--vk", outA, 64), 0) << "MinimalSample failed to run";
    ASSERT_EQ(RunSample("MinimalGISample", "--minimal-sample-compatibility-mode --vk", outB, 64), 0) << "MinimalGISample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "MinimalDI vs MinimalGI_DI"));
}

TEST(SampleImageTests, MinimalGI_vs_Intermediate_DI)
{
    fs::path outA = GetOutputDir() / "MinimalGI_vs_Intermediate_DI_A.bmp";
    fs::path outB = GetOutputDir() / "MinimalGI_vs_Intermediate_DI_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample", "--disable-gi --minimal-sample-compatibility-mode", outA, 64), 0) << "MinimalGISample failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--minimal-sample-compatibility-mode", outB, 64), 0)
        << "IntermediateSample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "MinimalGI vs Intermediate (no GI)"));
}

TEST(SampleImageTests, MinimalGI_vs_Intermediate_DI_Frame1)
{
    // Diagnostic parity test: compare frame 1 to isolate first-frame differences
    // (initial sampling/shading/G-buffer/light prep) from temporal history effects.
    // Keep this test to quickly detect structural mismatches before resampling
    // history and accumulation can mask or redistribute errors.
    fs::path outA = GetOutputDir() / "MinimalGI_vs_Intermediate_DI_F1_A.bmp";
    fs::path outB = GetOutputDir() / "MinimalGI_vs_Intermediate_DI_F1_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample", "--disable-gi --minimal-sample-compatibility-mode", outA, 1), 0) << "MinimalGISample failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--minimal-sample-compatibility-mode", outB, 1), 0)
        << "IntermediateSample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "MinimalGI vs Intermediate (no GI, frame 1)"));
}

TEST(SampleImageTests, MinimalGI_vs_Intermediate_GI)
{
    fs::path outA = GetOutputDir() / "MinimalGI_vs_Intermediate_GI_A.bmp";
    fs::path outB = GetOutputDir() / "MinimalGI_vs_Intermediate_GI_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample", "", outA, 64), 0) << "MinimalGISample failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--indirect-mode RESTIRGI --aa-mode ACC", outB, 64), 0)
        << "IntermediateSample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "MinimalGI vs Intermediate (GI)"));
}

TEST(SampleImageTests, Intermediate_vs_Full_NoNRD)
{
    fs::path outA = GetOutputDir() / "IntermediateSample_NoNRD.bmp";
    fs::path outB = GetOutputDir() / "FullSample_NoNRD.bmp";

    ASSERT_EQ(RunSample("IntermediateSample", "--aa-mode ACC", outA, 128), 0)
        << "IntermediateSample failed to run";
    ASSERT_EQ(RunSample("FullSample", "--denoiser OFF --aa-mode ACC", outB, 128), 0)
        << "FullSample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "Intermediate vs Full (no NRD)"));
}

TEST(SampleImageTests, Full_DX12_vs_VK)
{
    fs::path outDX12 = GetOutputDir() / "FullSample_DX12.bmp";
    fs::path outVK   = GetOutputDir() / "FullSample_VK.bmp";

    ASSERT_EQ(RunSample("FullSample", "--aa-mode DLSS --denoiser RELAX", outDX12, 128), 0)
        << "FullSample (DX12) failed to run";
    ASSERT_EQ(RunSample("FullSample", "--vk --aa-mode DLSS --denoiser RELAX", outVK, 128), 0)
        << "FullSample (VK) failed to run";
    EXPECT_TRUE(CompareImages(outDX12, outVK, "FullSample DX12 vs VK"));
}
