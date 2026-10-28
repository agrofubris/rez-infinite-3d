cmake_minimum_required(VERSION 3.20)
project(xinput_proxy LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_library(xinput1_3 SHARED xinput_proxy.cpp xinput1_3.def)
target_compile_definitions(xinput1_3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
set_target_properties(xinput1_3 PROPERTIES PREFIX "" OUTPUT_NAME "xinput1_3")
