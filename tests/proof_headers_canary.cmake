if(NOT DEFINED GENERATED_DIRECTORY OR NOT IS_DIRECTORY "${GENERATED_DIRECTORY}")
    message(FATAL_ERROR "generated SDK directory was not provided")
endif()

if(NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "proof-header output directory was not provided")
endif()

file(REMOVE_RECURSE "${OUTPUT_DIRECTORY}")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")

set(project_directory "${OUTPUT_DIRECTORY}/project")
file(MAKE_DIRECTORY "${project_directory}")

set(cmake_text [=[
cmake_minimum_required(VERSION 3.20)
project(tmfdev_proof_headers LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
]=])

foreach(header
    CAudioPort.hpp
    CGameApp.hpp
    CMwNod.hpp
    CSceneMobil.hpp
    CTrackManiaControlPlayerInput.hpp
    CTrackManiaEditor.hpp
)
    string(REPLACE ".hpp" "" stem "${header}")
    set(target "tmfdev_header_${stem}")
    set(source "${project_directory}/${stem}.cpp")

    file(WRITE "${source}"
        "#include \"${header}\"\n"
        "static_assert(tmf::build::ImageBase == 0x00400000u);\n"
        "int main() { return 0; }\n"
    )

    string(APPEND cmake_text
        "add_executable(${target} \"${source}\")\n"
        "target_include_directories(${target} PRIVATE \"${GENERATED_DIRECTORY}\")\n"
    )
endforeach()

file(WRITE "${project_directory}/CMakeLists.txt" "${cmake_text}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${project_directory}" -B "${project_directory}/build"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error
)

if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR
        "standalone proof-header configure failed (${configure_result}):\n"
        "${configure_error}\n${configure_output}"
    )
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${project_directory}/build" --config Debug
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_error
)

if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR
        "standalone proof-header compile failed (${compile_result}):\n"
        "${compile_error}\n${compile_output}"
    )
endif()

message(STATUS "Standalone proof-header canary passed: six headers compiled independently")
