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

// Reads back a GPU texture to CPU, handles BGRA -> RGBA conversion,
// and writes a BMP file.  Shared by all sample applications.
bool SaveTexture(nvrhi::IDevice* device, nvrhi::ITexture* texture, const char* writeFileName);
