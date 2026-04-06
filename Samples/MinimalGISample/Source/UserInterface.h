/***************************************************************************
 # Copyright (c) 2021-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <memory>
#include <donut/engine/Scene.h>
#include <donut/app/imgui_renderer.h>
#include "RenderPass.h"

struct UIData
{
    bool reloadShaders = false;
    bool showUI = true;
    bool isLoading = true;
    
    RenderPass::Settings lightingSettings;
};

class UserInterface : public donut::app::ImGui_Renderer
{
public:
    UserInterface(donut::app::DeviceManager* deviceManager, donut::vfs::IFileSystem& rootFS, UIData& ui);

protected:
    void buildUI() override;

private:
    UIData& m_ui;
    std::shared_ptr<donut::app::RegisteredFont> m_fontOpenSans = nullptr;
};
