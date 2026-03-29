set(project RtxdiSampleTests)

add_executable(${project}
    SampleTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/stb_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/ImageIO.cpp)

target_link_libraries(${project} RtxdiTestUtils)
set_target_properties(${project} PROPERTIES FOLDER "RTXDI SDK/Tests")

# Register each test case with CTest.
add_test(NAME MinimalDI_vs_MinimalGI_DI      COMMAND ${project} MinimalDI_vs_MinimalGI_DI)
add_test(NAME MinimalGI_vs_Intermediate_NoDI  COMMAND ${project} MinimalGI_vs_Intermediate_NoDI)
add_test(NAME MinimalGI_vs_Intermediate_GI    COMMAND ${project} MinimalGI_vs_Intermediate_GI)
add_test(NAME Intermediate_vs_Full_NoNRD      COMMAND ${project} Intermediate_vs_Full_NoNRD)
add_test(NAME Full_DX12_Baseline              COMMAND ${project} Full_DX12_Baseline)
add_test(NAME Full_VK_Baseline                COMMAND ${project} Full_VK_Baseline)
