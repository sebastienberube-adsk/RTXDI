set(project RtxdiSampleTests)

# Fetch Google Test for VS Test Explorer integration
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    URL_HASH SHA256=f179ec217f9b3b3f3c6e8b02d3e7eda997b49e4ce26d6b235c9053bec9c0bf9f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(${project}
    SampleTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/stb_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/ImageIO.cpp)

target_link_libraries(${project} RtxdiTestUtils GTest::gtest_main)
set_target_properties(${project} PROPERTIES FOLDER "RTXDI SDK/Tests")

if (TARGET gtest)
    set_target_properties(gtest      PROPERTIES FOLDER "RTXDI SDK/Tests/GoogleTest")
    set_target_properties(gtest_main PROPERTIES FOLDER "RTXDI SDK/Tests/GoogleTest")
    set_target_properties(gmock      PROPERTIES FOLDER "RTXDI SDK/Tests/GoogleTest")
    set_target_properties(gmock_main PROPERTIES FOLDER "RTXDI SDK/Tests/GoogleTest")
endif()

# gtest_discover_tests uses POST_BUILD discovery so VS Test Explorer
# sees each TEST() individually without needing to run the exe at cmake time.
include(GoogleTest)
gtest_discover_tests(${project}
    DISCOVERY_MODE POST_BUILD
    PROPERTIES TIMEOUT 300)
