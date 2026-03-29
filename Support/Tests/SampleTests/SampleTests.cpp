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

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

static constexpr bool kStrictImageComparison = false;

static StochasticThresholds GetThresholds()
{
    StochasticThresholds t;
    t.tileSize                = 16;
    t.absAvgDeltaThreshold    = kStrictImageComparison ? 0.0f : 0.05f;
    t.relDifferenceThreshold  = kStrictImageComparison ? 0.0f : 0.10f;
    t.absStdDevDeltaThreshold = kStrictImageComparison ? 0.0f : 0.07f;
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

static fs::path GetBaselineDir()
{
    return GetRepoRoot() / "Support" / "Tests" / "Baselines";
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
    // cmd.exe strips the outermost pair of quotes, so wrap everything in an extra layer.
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

    auto result = CompareStochastic(pixA.data(), pixB.data(), wA, hA, GetThresholds());
    std::cout << "  [" << label << "] " << result.summary;

    if (result.passed)
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << result.summary;
}

// ---------------------------------------------------------------------------
// Test cases -- each TEST() appears individually in VS Test Explorer.
// ---------------------------------------------------------------------------

TEST(SampleImageTests, MinimalDI_vs_MinimalGI_DI)
{
    fs::path outA = GetOutputDir() / "MinimalSample_DI.bmp";
    fs::path outB = GetOutputDir() / "MinimalGISample_DI.bmp";

    ASSERT_EQ(RunSample("MinimalSample", "", outA, 64), 0) << "MinimalSample failed to run";
    ASSERT_EQ(RunSample("MinimalGISample", "--disable-gi", outB, 64), 0) << "MinimalGISample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "MinimalDI vs MinimalGI_DI"));
}

TEST(SampleImageTests, MinimalGI_vs_Intermediate_NoDI)
{
    fs::path outA = GetOutputDir() / "MinimalGISample_DI.bmp";
    fs::path outB = GetOutputDir() / "IntermediateSample_NoDI.bmp";

    ASSERT_EQ(RunSample("MinimalGISample", "--disable-gi", outA, 64), 0) << "MinimalGISample failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--indirect-mode NONE --aa-mode ACC", outB, 128), 0)
        << "IntermediateSample failed to run";
    EXPECT_TRUE(CompareImages(outA, outB, "MinimalGI vs Intermediate (no GI)"));
}

TEST(SampleImageTests, MinimalGI_vs_Intermediate_GI)
{
    fs::path outA = GetOutputDir() / "MinimalGISample_GI.bmp";
    fs::path outB = GetOutputDir() / "IntermediateSample_GI.bmp";

    ASSERT_EQ(RunSample("MinimalGISample", "", outA, 64), 0) << "MinimalGISample failed to run";
    ASSERT_EQ(RunSample("IntermediateSample", "--indirect-mode RESTIRGI --aa-mode ACC", outB, 128), 0)
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

TEST(SampleImageTests, Full_DX12_Baseline)
{
    fs::path output = GetOutputDir() / "FullSample_DX12.bmp";
    fs::path baseline = GetBaselineDir() / "Full_DX12_Baseline.bmp";

    ASSERT_EQ(RunSample("FullSample", "--aa-mode DLSS --denoiser RELAX", output, 128), 0)
        << "FullSample failed to run";

    if (!fs::exists(baseline))
    {
        std::cout << "  Baseline not found. Saving current output as new baseline: "
                  << baseline << std::endl;
        fs::copy_file(output, baseline, fs::copy_options::overwrite_existing);
        std::cout << "  PASS (baseline generated -- re-run to validate)" << std::endl;
        return;
    }

    EXPECT_TRUE(CompareImages(output, baseline, "FullSample DX12 vs baseline"));
}

TEST(SampleImageTests, Full_VK_Baseline)
{
    fs::path output = GetOutputDir() / "FullSample_VK.bmp";
    fs::path baseline = GetBaselineDir() / "Full_DX12_Baseline.bmp";

    ASSERT_EQ(RunSample("FullSample", "--vk --aa-mode DLSS --denoiser RELAX", output, 128), 0)
        << "FullSample failed to run";

    ASSERT_TRUE(fs::exists(baseline))
        << "DX12 baseline not found at " << baseline << ". Run Full_DX12_Baseline first.";

    EXPECT_TRUE(CompareImages(output, baseline, "FullSample VK vs DX12 baseline"));
}
