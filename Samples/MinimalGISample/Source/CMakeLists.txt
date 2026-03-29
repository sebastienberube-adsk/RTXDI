
file(GLOB sources "*.cpp" "*.h")

set(project MinimalGISample)
set(folder "RTXDI SDK")

include(CMakeDependentOption)

# ImageIO.cpp provides LoadImageRGBA8 (stb_image impl comes from donut_engine)
list(APPEND sources "${CMAKE_CURRENT_SOURCE_DIR}/../../../Support/Tests/Common/ImageIO.cpp")

add_executable(${project} WIN32 ${sources})

target_link_libraries(${project} donut_core donut_engine donut_app donut_render Rtxdi cxxopts RtxdiTestUtils)
add_dependencies(${project} MinimalGISampleShaders)
set_target_properties(${project} PROPERTIES FOLDER ${folder})
