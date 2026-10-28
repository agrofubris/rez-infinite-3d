cmake_minimum_required(VERSION 3.20)
project(rez3d LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(openvr_api SHARED
    src/shim.cpp
    src/presenter.cpp
    src/config.cpp
    src/log.cpp
    src/audio.cpp
)

target_include_directories(openvr_api PRIVATE src vendor)
target_compile_definitions(openvr_api PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(openvr_api PRIVATE d3d11 dxgi d3dcompiler ole32 shell32)

set_target_properties(openvr_api PROPERTIES
    PREFIX ""
    OUTPUT_NAME "openvr_api"
)

if(MSVC)
    target_compile_options(openvr_api PRIVATE /W3 /permissive- /utf-8)
endif()
