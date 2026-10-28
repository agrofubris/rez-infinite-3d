cmake_minimum_required(VERSION 3.20)
project(rez3d_leia_sr LANGUAGES CXX)

# Optional Leia SR loader DLL. Needs a checkout of SR-lib (bo3b, branch
# api_expansion, commit 0c80cc01f671e1491ea70f8f9c2358ff938a6503), which vendors
# the Leia SR SDK 1.34.10. Neither is part of this repository.
#
#   cmake -S leia -B leia/build -A x64 -DSRLIB_DIR=<path to SR-lib>
set(SRLIB_DIR "" CACHE PATH "SR-lib checkout (contains SR.cpp and SR-SDK-1.34.10)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT MSVC OR NOT EXISTS "${SRLIB_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Leia SR: set SRLIB_DIR to an SR-lib checkout (MSVC only).")
endif()

add_subdirectory("${SRLIB_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/sr-lib")
if(NOT SRLIB_FOUND)
    message(FATAL_ERROR "Leia SR: SR-lib found but its SDK import libraries are missing.")
endif()

add_library(rez3d_leia_sr SHARED rez3d_leia_sr.cpp)
target_link_libraries(rez3d_leia_sr PRIVATE SRLib::SR d3d11)
# Every SR runtime DLL is delay-loaded: this DLL loads on any machine
srlib_apply_delayload(rez3d_leia_sr)
target_compile_definitions(rez3d_leia_sr PRIVATE REZ3D_LEIA_SR_BUILDING WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(rez3d_leia_sr PRIVATE /W3 /utf-8 /EHsc)
set_target_properties(rez3d_leia_sr PROPERTIES PREFIX "" OUTPUT_NAME "rez3d_leia_sr")
message(STATUS "Leia SR: building rez3d_leia_sr.dll against ${SRLIB_SDKROOT}")
