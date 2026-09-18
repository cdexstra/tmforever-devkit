cmake_minimum_required(VERSION 3.20)

project(tmforever_package_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TMFDEV_PACKAGE "" CACHE PATH "Path to a generated tmfdev class package")

if(NOT IS_DIRECTORY "${TMFDEV_PACKAGE}")
    message(FATAL_ERROR "TMFDEV_PACKAGE must point to a generated tmfdev package")
endif()

set(TMFDEV_CMAKE "${TMFDEV_PACKAGE}/tmfdev.cmake")
if(NOT EXISTS "${TMFDEV_CMAKE}")
    message(FATAL_ERROR "tmfdev package is missing tmfdev.cmake: ${TMFDEV_CMAKE}")
endif()

include("${TMFDEV_CMAKE}")

add_executable(tmforever_package_consumer
    consumer.cpp
)

target_link_libraries(tmforever_package_consumer PRIVATE
    tmfdev::sdk
)
