
file(GLOB sources "*.cpp" "*.h")

set(project MinimalSample)
set(folder "RTXDI SDK")

include(CMakeDependentOption)

list(APPEND sources "${CMAKE_CURRENT_SOURCE_DIR}/../../Common/SaveTexture.cpp")

add_executable(${project} WIN32 ${sources})

target_include_directories(${project} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../Common")
target_link_libraries(${project} donut_core donut_engine donut_app donut_render Rtxdi cxxopts)
add_dependencies(${project} MinimalSampleShaders)
set_target_properties(${project} PROPERTIES FOLDER ${folder})
