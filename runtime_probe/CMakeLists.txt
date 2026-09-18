cmake_minimum_required(VERSION 3.20)

project(tmf_runtime_probe LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TMFDEV_MINHOOK_DIR "" CACHE PATH
    "Path to the MinHook source directory")

if(NOT EXISTS "${TMFDEV_MINHOOK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "TMFDEV_MINHOOK_DIR must point to the MinHook source directory")
endif()

add_subdirectory(
    "${TMFDEV_MINHOOK_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/minhook"
    EXCLUDE_FROM_ALL
)

add_library(tmfdev_runtime STATIC
    "../generated/tmfdev_runtime.cpp"
)
target_include_directories(tmfdev_runtime PUBLIC
    "../generated"
)
target_link_libraries(tmfdev_runtime PUBLIC bcrypt)

add_library(tmf_runtime_probe SHARED probe.cpp)
target_link_libraries(tmf_runtime_probe PRIVATE tmfdev_runtime minhook)

add_executable(tmf_probe_injector injector.cpp)
