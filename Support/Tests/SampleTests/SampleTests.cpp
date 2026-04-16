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
#include "SampleTests.h"
#include <gtest/gtest.h>

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
                     const fs::path& outputFile, uint32_t saveFrame,
                     const char* scene = kScene)
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
        + " --scene " + scene
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

static ::testing::AssertionResult CompareImages(
    const fs::path& imageA, const fs::path& imageB, const std::string& label,
    const StochasticThresholds& thresholds = GetThresholds())
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
TEST(SampleImageTests, T00_Threshold_MinimalSample_SelfComparison)
{
    fs::path outA = GetOutputDir() / "T00_Threshold_MinimalSample_SelfComparison_A.bmp";
    fs::path outB = GetOutputDir() / "T00_Threshold_MinimalSample_SelfComparison_B.bmp";

    ASSERT_EQ(RunSample("MinimalSample", "", outA, 50), 0) << "MinimalSample (frame 50) failed to run";
    ASSERT_EQ(RunSample("MinimalSample", "", outB, 75), 0) << "MinimalSample (frame 75) failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "T00_Threshold_MinimalSample_SelfComparison"));
}

TEST(SampleImageTests, T00_Threshold_IntermediateSample_SelfComparison)
{
    fs::path outA = GetOutputDir() / "T00_Threshold_IntermediateSample_SelfComparison_A.bmp";
    fs::path outB = GetOutputDir() / "T00_Threshold_IntermediateSample_SelfComparison_B.bmp";

    ASSERT_EQ(RunSample("IntermediateSample", "--minimal-gi-sample-compatibility-mode-di", outA, 64), 0)
        << "IntermediateSample (frame 64) failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--minimal-gi-sample-compatibility-mode-di", outB, 128), 0)
        << "IntermediateSample (frame 128) failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "T00_Threshold_IntermediateSample_SelfComparison"));
}

TEST(SampleImageTests, T00_Minimal_VK_vs_Minimal_DX)
{
    fs::path outA = GetOutputDir() / "T00_Minimal_VK_vs_Minimal_DX_A.bmp";
    fs::path outB = GetOutputDir() / "T00_Minimal_VK_vs_Minimal_DX_B.bmp";

    ASSERT_EQ(RunSample("MinimalSample", "", outA, 64), 0) << "MinimalSample failed to run";
    ASSERT_EQ(RunSample("MinimalSample", "--vk", outB, 64), 0) << "MinimalSample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "T00_Minimal_VK_vs_Minimal_DX"));
}

TEST(SampleImageTests, T01_Minimal_vs_MinimalGI_DI_DX)
{
    fs::path outA = GetOutputDir() / "T01_Minimal_vs_MinimalGI_DI_DX_A.bmp";
    fs::path outB = GetOutputDir() / "T01_Minimal_vs_MinimalGI_DI_DX_B.bmp";

    ASSERT_EQ(RunSample("MinimalSample", "", outA, 64), 0) << "MinimalSample failed to run";
    ASSERT_EQ(RunSample("MinimalGISample", "--minimal-sample-compatibility-mode", outB, 64), 0) << "MinimalGISample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "T01_Minimal_vs_MinimalGI_DI_DX"));
}

TEST(SampleImageTests, T01_Minimal_vs_MinimalGI_DI_VK)
{
    fs::path outA = GetOutputDir() / "T01_Minimal_vs_MinimalGI_DI_VK_A.bmp";
    fs::path outB = GetOutputDir() / "T01_Minimal_vs_MinimalGI_DI_VK_B.bmp";

    ASSERT_EQ(RunSample("MinimalSample", "--vk", outA, 64), 0) << "MinimalSample failed to run";
    ASSERT_EQ(RunSample("MinimalGISample", "--minimal-sample-compatibility-mode --vk", outB, 64), 0) << "MinimalGISample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "T01_Minimal_vs_MinimalGI_DI_VK"));
}

/*TEST(SampleImageTests, MinimalGI_vs_Intermediate_DI_Frame1)
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
}*/

TEST(SampleImageTests, T02_MinimalGI_vs_Intermediate_DI_DX)
{
    fs::path outA = GetOutputDir() / "T02_MinimalGI_vs_Intermediate_DI_DX_A.bmp";
    fs::path outB = GetOutputDir() / "T02_MinimalGI_vs_Intermediate_DI_DX_B.bmp";
    
    ASSERT_EQ(RunSample("MinimalGISample", "--indirect-mode NONE --intermediate-sample-compatibility-mode", outA, 64), 0) << "MinimalGISample failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--minimal-gi-sample-compatibility-mode-di", outB, 64), 0)
        << "IntermediateSample failed to run";
    StochasticThresholds t = GetThresholds();
    t.imageAbsAvgDeltaThreshold = 0.075f;
    t.imageRelDifferenceThreshold = 0.100f;
    EXPECT_TRUE(CompareImages(outA, outB, "T02_MinimalGI_vs_Intermediate_DI_DX", t));
}

TEST(SampleImageTests, T02_MinimalGI_vs_Intermediate_DI_VK)
{
    fs::path outA = GetOutputDir() / "T02_MinimalGI_vs_Intermediate_DI_VK_A.bmp";
    fs::path outB = GetOutputDir() / "T02_MinimalGI_vs_Intermediate_DI_VK_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample", "--indirect-mode NONE --intermediate-sample-compatibility-mode --vk", outA, 64), 0) << "MinimalGISample failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--minimal-gi-sample-compatibility-mode-di --vk", outB, 64), 0)
        << "IntermediateSample failed to run";
    StochasticThresholds t = GetThresholds();
    t.imageAbsAvgDeltaThreshold = 0.075f;
    t.imageRelDifferenceThreshold = 0.100f;
    EXPECT_TRUE(CompareImages(outA, outB, "T02_MinimalGI_vs_Intermediate_DI_VK", t));
}

TEST(SampleImageTests, T03_MinimalGI_vs_Intermediate_GI_DX)
{
    fs::path outA = GetOutputDir() / "T03_MinimalGI_vs_Intermediate_GI_DX_A.bmp";
    fs::path outB = GetOutputDir() / "T03_MinimalGI_vs_Intermediate_GI_DX_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample", "--indirect-mode RESTIRGI --intermediate-sample-compatibility-mode", outA, 64), 0) << "MinimalGISample failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--indirect-mode RESTIRGI --minimal-gi-sample-compatibility-mode-gi", outB, 64), 0)
        << "IntermediateSample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "T03_MinimalGI_vs_Intermediate_GI_DX"));
}

TEST(SampleImageTests, T04_MinimalGI_vs_Intermediate_GI_VK)
{
    fs::path outA = GetOutputDir() / "T04_MinimalGI_vs_Intermediate_GI_VK_A.bmp";
    fs::path outB = GetOutputDir() / "T04_MinimalGI_vs_Intermediate_GI_VK_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample", "--indirect-mode RESTIRGI --intermediate-sample-compatibility-mode --vk", outA, 64), 0) << "MinimalGISample failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--indirect-mode RESTIRGI --minimal-gi-sample-compatibility-mode-gi --vk", outB, 64), 0)
        << "IntermediateSample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "T04_MinimalGI_vs_Intermediate_GI_VK"));
}

TEST(SampleImageTests, T05_MinimalGI_vs_Intermediate_GI_Acc_DX)
{
    fs::path outA = GetOutputDir() / "T05_MinimalGI_vs_Intermediate_GI_Acc_DX_A.bmp";
    fs::path outB = GetOutputDir() / "T05_MinimalGI_vs_Intermediate_GI_Acc_DX_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample", "--indirect-mode RESTIRGI --intermediate-sample-compatibility-mode --aa-mode ACC", outA, 64), 0) << "MinimalGISample failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--indirect-mode RESTIRGI --minimal-gi-sample-compatibility-mode-gi --aa-mode ACC", outB, 64), 0)
        << "IntermediateSample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "T05_MinimalGI_vs_Intermediate_GI_Acc_DX"));
}

TEST(SampleImageTests, T05_MinimalGI_vs_Intermediate_GI_Acc_VK)
{
    fs::path outA = GetOutputDir() / "T05_MinimalGI_vs_Intermediate_GI_Acc_VK_A.bmp";
    fs::path outB = GetOutputDir() / "T05_MinimalGI_vs_Intermediate_GI_Acc_VK_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample", "--indirect-mode RESTIRGI --intermediate-sample-compatibility-mode --aa-mode ACC --vk", outA, 64), 0) << "MinimalGISample failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--indirect-mode RESTIRGI --minimal-gi-sample-compatibility-mode-gi --aa-mode ACC --vk", outB, 64), 0)
        << "IntermediateSample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "T05_MinimalGI_vs_Intermediate_GI_Acc_VK"));
}

TEST(SampleImageTests, T06_MinimalGI_vs_Intermediate_Beauty_Shot_DX)
{
    static const char* kBeautyShotScene = "/Assets/Media/livingroom_Sun.scene.json";

    fs::path outA = GetOutputDir() / "T06_MinimalGI_vs_Intermediate_Beauty_Shot_DX_A.bmp";
    fs::path outB = GetOutputDir() / "T06_MinimalGI_vs_Intermediate_Beauty_Shot_DX_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample", "--beauty-shot-mode --rtxdi-tonemap-bias 0.003", outA, 128, kBeautyShotScene), 0) << "MinimalGISample failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--beauty-shot-mode --rtxdi-tonemap-bias 0.003", outB, 128, kBeautyShotScene), 0)
        << "IntermediateSample failed to run";
    StochasticThresholds t = GetThresholds();
    t.imageAbsAvgDeltaThreshold = 0.045f;
    t.imageRelDifferenceThreshold = 0.070f;
    EXPECT_TRUE(CompareImages(outA, outB, "T06_MinimalGI_vs_Intermediate_Beauty_Shot_DX", t));
}

TEST(SampleImageTests, T06_MinimalGI_vs_Intermediate_Beauty_Shot_VK)
{
    static const char* kBeautyShotScene = "/Assets/Media/livingroom_Sun.scene.json";

    fs::path outA = GetOutputDir() / "T06_MinimalGI_vs_Intermediate_Beauty_Shot_VK_A.bmp";
    fs::path outB = GetOutputDir() / "T06_MinimalGI_vs_Intermediate_Beauty_Shot_VK_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample", "--beauty-shot-mode --rtxdi-tonemap-bias 0.003 --vk", outA, 128, kBeautyShotScene), 0) << "MinimalGISample failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--beauty-shot-mode --rtxdi-tonemap-bias 0.003 --vk", outB, 128, kBeautyShotScene), 0)
        << "IntermediateSample failed to run";
    StochasticThresholds t = GetThresholds();
    t.imageAbsAvgDeltaThreshold = 0.045f;
    t.imageRelDifferenceThreshold = 0.070f;
    EXPECT_TRUE(CompareImages(outA, outB, "T06_MinimalGI_vs_Intermediate_Beauty_Shot_VK", t));
}

TEST(SampleImageTests, T98_Intermediate_vs_Full_NoNRD_DX)
{
    fs::path outA = GetOutputDir() / "T98_Intermediate_vs_Full_NoNRD_DX_A.bmp";
    fs::path outB = GetOutputDir() / "T98_Intermediate_vs_Full_NoNRD_DX_B.bmp";

    ASSERT_EQ(RunSample("IntermediateSample", "--aa-mode ACC", outA, 128), 0)
        << "IntermediateSample failed to run";
    ASSERT_EQ(RunSample("FullSample", "--denoiser OFF --aa-mode ACC", outB, 128), 0)
        << "FullSample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "T98_Intermediate_vs_Full_NoNRD_DX"));
}

TEST(SampleImageTests, T99_Full_DX12_vs_VK)
{
    fs::path outDX12 = GetOutputDir() / "T99_Full_DX12_vs_VK_A.bmp";
    fs::path outVK   = GetOutputDir() / "T99_Full_DX12_vs_VK_B.bmp";

    ASSERT_EQ(RunSample("FullSample", "--aa-mode DLSS --denoiser RELAX", outDX12, 128), 0)
        << "FullSample (DX12) failed to run";
    ASSERT_EQ(RunSample("FullSample", "--vk --aa-mode DLSS --denoiser RELAX", outVK, 128), 0)
        << "FullSample (VK) failed to run";
    EXPECT_TRUE(CompareImages(outDX12, outVK, "T99_Full_DX12_vs_VK"));
}

// ---------------------------------------------------------------------------
// G-Buffer diagnostic comparison tests
// These tests compare individual G-buffer channels between MinimalGISample
// and IntermediateSample using --diag-mode to bypass lighting and output
// raw G-buffer values. Frame 1 is used to avoid temporal accumulation.
// ---------------------------------------------------------------------------

static const char* kMinimalGIDiagArgs  = "--indirect-mode NONE --intermediate-sample-compatibility-mode --diag-mode ";
static const char* kIntermediateDiagArgs = "--minimal-gi-sample-compatibility-mode-di --diag-mode ";

static StochasticThresholds GetGBufferThresholds()
{
    StochasticThresholds t = GetThresholds();
    t.absAvgDeltaThreshold    = 0.02f;
    t.relDifferenceThreshold  = 0.10f;
    t.absStdDevDeltaThreshold = 0.05f;
    t.imageAbsAvgDeltaThreshold  = 0.01f;
    t.imageRelDifferenceThreshold = 0.02f;
    return t;
}

TEST(SampleImageTests, T00_GBuffer_Roughness_MinimalGI_vs_Intermediate)
{
    fs::path outA = GetOutputDir() / "T00_GBuffer_Roughness_MinimalGI_vs_Intermediate_A.bmp";
    fs::path outB = GetOutputDir() / "T00_GBuffer_Roughness_MinimalGI_vs_Intermediate_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample",    std::string(kMinimalGIDiagArgs) + "1", outA, 1), 0) << "MinimalGISample failed";
    ASSERT_EQ(RunSample("IntermediateSample", std::string(kIntermediateDiagArgs) + "1", outB, 1), 0) << "IntermediateSample failed";
    EXPECT_TRUE(CompareImages(outA, outB, "T00_GBuffer_Roughness_MinimalGI_vs_Intermediate", GetGBufferThresholds()));
}

TEST(SampleImageTests, T00_GBuffer_Normals_MinimalGI_vs_Intermediate)
{
    fs::path outA = GetOutputDir() / "T00_GBuffer_Normals_MinimalGI_vs_Intermediate_A.bmp";
    fs::path outB = GetOutputDir() / "T00_GBuffer_Normals_MinimalGI_vs_Intermediate_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample",    std::string(kMinimalGIDiagArgs) + "2", outA, 1), 0) << "MinimalGISample failed";
    ASSERT_EQ(RunSample("IntermediateSample", std::string(kIntermediateDiagArgs) + "2", outB, 1), 0) << "IntermediateSample failed";
    EXPECT_TRUE(CompareImages(outA, outB, "T00_GBuffer_Normals_MinimalGI_vs_Intermediate", GetGBufferThresholds()));
}

TEST(SampleImageTests, T00_GBuffer_DiffuseAlbedo_MinimalGI_vs_Intermediate)
{
    fs::path outA = GetOutputDir() / "T00_GBuffer_DiffuseAlbedo_MinimalGI_vs_Intermediate_A.bmp";
    fs::path outB = GetOutputDir() / "T00_GBuffer_DiffuseAlbedo_MinimalGI_vs_Intermediate_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample",    std::string(kMinimalGIDiagArgs) + "3", outA, 1), 0) << "MinimalGISample failed";
    ASSERT_EQ(RunSample("IntermediateSample", std::string(kIntermediateDiagArgs) + "3", outB, 1), 0) << "IntermediateSample failed";
    EXPECT_TRUE(CompareImages(outA, outB, "T00_GBuffer_DiffuseAlbedo_MinimalGI_vs_Intermediate_B", GetGBufferThresholds()));
}

TEST(SampleImageTests, T00_GBuffer_SpecularF0_MinimalGI_vs_Intermediate)
{
    fs::path outA = GetOutputDir() / "T00_GBuffer_SpecularF0_MinimalGI_vs_Intermediate_A.bmp";
    fs::path outB = GetOutputDir() / "T00_GBuffer_SpecularF0_MinimalGI_vs_Intermediate_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample",    std::string(kMinimalGIDiagArgs) + "4", outA, 1), 0) << "MinimalGISample failed";
    ASSERT_EQ(RunSample("IntermediateSample", std::string(kIntermediateDiagArgs) + "4", outB, 1), 0) << "IntermediateSample failed";
    EXPECT_TRUE(CompareImages(outA, outB, "T00_GBuffer_SpecularF0_MinimalGI_vs_Intermediate", GetGBufferThresholds()));
}

TEST(SampleImageTests, T00_GBuffer_Depth_MinimalGI_vs_Intermediate)
{
    fs::path outA = GetOutputDir() / "T00_GBuffer_Depth_MinimalGI_vs_Intermediate_A.bmp";
    fs::path outB = GetOutputDir() / "T00_GBuffer_Depth_MinimalGI_vs_Intermediate_B.bmp";

    ASSERT_EQ(RunSample("MinimalGISample",    std::string(kMinimalGIDiagArgs) + "5", outA, 1), 0) << "MinimalGISample failed";
    ASSERT_EQ(RunSample("IntermediateSample", std::string(kIntermediateDiagArgs) + "5", outB, 1), 0) << "IntermediateSample failed";
    EXPECT_TRUE(CompareImages(outA, outB, "T00_GBuffer_Depth_MinimalGI_vs_Intermediate", GetGBufferThresholds()));
}
