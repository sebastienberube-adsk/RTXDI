set(project RtxdiTestUtils)

# Core comparison library -- no stb_image dependency.
add_library(${project} STATIC
    ImageComparison.h
    ImageComparison.cpp)

target_include_directories(${project} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# stb_image headers (needed by consumers that compile ImageIO.cpp)
target_include_directories(${project} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../External/donut/thirdparty/stb)

set_target_properties(${project} PROPERTIES FOLDER "RTXDI SDK/Tests")
