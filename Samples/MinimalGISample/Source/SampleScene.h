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

#include <donut/engine/Scene.h>

class SampleScene : public donut::engine::Scene
{
public:
    using Scene::Scene;
    
    void BuildMeshBLASes(nvrhi::IDevice* device);
    void BuildTopLevelAccelStruct(nvrhi::ICommandList* commandList);

    nvrhi::rt::IAccelStruct* GetTopLevelAS() const;

private:
    nvrhi::rt::AccelStructHandle m_topLevelAS;
    std::vector<nvrhi::rt::InstanceDesc> m_tlasInstances;

    bool m_canUpdateTLAS = false;

    double m_wallclockTime = 0;
};
