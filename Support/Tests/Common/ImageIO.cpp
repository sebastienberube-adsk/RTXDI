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

#include <stb_image.h>

#include <iostream>

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
