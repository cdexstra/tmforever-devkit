if(NOT DEFINED TMFDEV OR NOT EXISTS "${TMFDEV}")
    message(FATAL_ERROR "tmfdev executable was not provided")
endif()

if(NOT DEFINED FIXTURE_EXE OR NOT EXISTS "${FIXTURE_EXE}")
    message(FATAL_ERROR "TMForever executable fixture was not provided")
endif()

if(NOT DEFINED FIXTURE_MAP OR NOT EXISTS "${FIXTURE_MAP}")
    message(FATAL_ERROR "TMForever MAP fixture was not provided")
endif()

if(NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "package output directory was not provided")
endif()

file(REMOVE_RECURSE "${OUTPUT_DIRECTORY}")

execute_process(
    COMMAND "${TMFDEV}" generate-package "${OUTPUT_DIRECTORY}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE generate_result
    OUTPUT_VARIABLE generate_output
    ERROR_VARIABLE generate_error
)

if(NOT generate_result EQUAL 0)
    message(FATAL_ERROR "package generation failed (${generate_result}): ${generate_error}")
endif()

foreach(expected_file
    "${OUTPUT_DIRECTORY}/README.md"
    "${OUTPUT_DIRECTORY}/package.json"
    "${OUTPUT_DIRECTORY}/tmfdev.cmake"
    "${OUTPUT_DIRECTORY}/include/TMForever_all.hpp"
    "${OUTPUT_DIRECTORY}/include/tmfdev/runtime.hpp"
    "${OUTPUT_DIRECTORY}/src/tmfdev_runtime.cpp"
    "${OUTPUT_DIRECTORY}/metadata/TMForever_manifest.json"
    "${OUTPUT_DIRECTORY}/metadata/search-index.tsv"
)
    if(NOT EXISTS "${expected_file}")
        message(FATAL_ERROR "package is missing ${expected_file}")
    endif()
endforeach()

file(READ "${OUTPUT_DIRECTORY}/package.json" package_json)
foreach(expected
    "\"schema\": 2"
    "\"sha256\": \"3847cf9f20bfc63914450060ed528c12104f743d96ad23d6e76abd178de8c84f\""
    "\"file_size\": 10498048"
    "\"sdk\": \"include/TMForever_all.hpp\""
    "\"runtime\": \"include/tmfdev/runtime.hpp\""
    "\"runtime_source\": \"src/tmfdev_runtime.cpp\""
    "\"search_index\": \"metadata/search-index.tsv\""
    "\"cmake\": \"tmfdev.cmake\""
    "\"emitted_functions\": 41049"
    "\"skipped_functions\": 3484"
    "\"artifact_sha256\""
)
    if(NOT package_json MATCHES "${expected}")
        message(FATAL_ERROR "package metadata missing '${expected}':\n${package_json}")
    endif()
endforeach()

file(READ "${OUTPUT_DIRECTORY}/include/tmfdev/runtime.hpp" runtime_header)
foreach(expected
    "GameAppAudioPortOffset"
    "audio_port()"
    "GameAppInputPortOffset"
    "input_port()"
    "TrackManiaVtableRva"
    "TrackManiaGetTmBlockEditorRva"
    "GameAppVtableRvas"
    "AudioPortVtableRvas"
    "InputPortVtableRvas"
    "BlockEditorVtableRvas"
    "PlaygroundVtableRvas"
    "block_editor()"
    "TrackManiaGetPlaygroundRva"
    "playground()"
    "file_sha256_verified()"
    "resolve_function"
)
    if(NOT runtime_header MATCHES "${expected}")
        message(FATAL_ERROR "runtime header missing '${expected}':\n${runtime_header}")
    endif()
endforeach()

if(runtime_header MATCHES "TMForever_all.hpp"
    OR runtime_header MATCHES "CGameApp.hpp")
    message(FATAL_ERROR
        "runtime header must remain independent of generated SDK class headers")
endif()

if(NOT runtime_header MATCHES "namespace build")
    message(FATAL_ERROR "runtime header is missing standalone build identity constants")
endif()

if(NOT runtime_header MATCHES "TMFDEV_GENERATED_BUILD_IDENTITY_CHECK"
    OR NOT runtime_header MATCHES "different executable builds")
    message(FATAL_ERROR "runtime header is missing cross-header build guard")
endif()

file(READ "${OUTPUT_DIRECTORY}/README.md" package_readme)
foreach(expected
    "Context::playground()"
    "CGamePlayground"
    "menu/editor/race transitions"
)
    if(NOT package_readme MATCHES "${expected}")
        message(FATAL_ERROR "package README missing runtime guidance '${expected}'")
    endif()
endforeach()

file(READ "${OUTPUT_DIRECTORY}/tmfdev.cmake" package_cmake_text)
foreach(expected
    "add_library(tmfdev_sdk INTERFACE)"
    "add_library(tmfdev::sdk ALIAS tmfdev_sdk)"
    "CMAKE_CURRENT_LIST_DIR}/include"
)
    string(FIND "${package_cmake_text}" "${expected}" expected_position)
    if(expected_position EQUAL -1)
        message(FATAL_ERROR "package CMake interface missing '${expected}':\n${package_cmake_text}")
    endif()
endforeach()

set(runtime_header_project "${OUTPUT_DIRECTORY}.runtime-header-only")
file(MAKE_DIRECTORY "${runtime_header_project}")
file(WRITE "${runtime_header_project}/runtime_header_smoke.cpp" [=[
#include <tmfdev/runtime.hpp>

static_assert(sizeof(void*) == 4);
static_assert(tmf::build::ImageBase != 0);

int main()
{
    const auto identity = tmf::runtime::ModuleIdentity{};
    return identity.image_size == 0
        && tmf::runtime::Status::ready == tmf::runtime::Status::ready
        ? 0
        : 1;
}
]=])

file(TO_CMAKE_PATH
    "${OUTPUT_DIRECTORY}/tmfdev.cmake"
    runtime_header_package_cmake
)
file(WRITE "${runtime_header_project}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.20)\n"
    "project(tmfdev_runtime_header_smoke LANGUAGES CXX)\n"
    "set(CMAKE_CXX_STANDARD 20)\n"
    "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
    "add_executable(tmfdev_runtime_header_smoke runtime_header_smoke.cpp)\n"
    "include(\"${runtime_header_package_cmake}\")\n"
    "target_link_libraries(tmfdev_runtime_header_smoke PRIVATE tmfdev::sdk)\n"
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${runtime_header_project}" -B
        "${runtime_header_project}/build" -A Win32
    RESULT_VARIABLE runtime_header_configure_result
    OUTPUT_VARIABLE runtime_header_configure_output
    ERROR_VARIABLE runtime_header_configure_error
)

if(NOT runtime_header_configure_result EQUAL 0)
    message(FATAL_ERROR
        "runtime-header-only project configure failed (${runtime_header_configure_result}):\n"
        "${runtime_header_configure_error}\n${runtime_header_configure_output}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build
        "${runtime_header_project}/build" --config Debug
    RESULT_VARIABLE runtime_header_compile_result
    OUTPUT_VARIABLE runtime_header_compile_output
    ERROR_VARIABLE runtime_header_compile_error
)

if(NOT runtime_header_compile_result EQUAL 0)
    message(FATAL_ERROR
        "runtime-header-only project did not compile (${runtime_header_compile_result}):\n"
        "${runtime_header_compile_error}\n${runtime_header_compile_output}")
endif()

file(READ "${OUTPUT_DIRECTORY}/metadata/search-index.tsv" search_index)
foreach(expected
    "# tmfdev search index schema 1"
    "kind\tid\tname\towner\trva\tphysical_code_id\tabi_status\tsdk_status\tdetails"
    "function\tfunction:CTrackManiaEditor::PlaceBlock@000AE110\t"
    "class\tclass:CTrackManiaEditor\tCTrackManiaEditor\t"
    "reflection\t"
    "category=action"
)
    if(NOT search_index MATCHES "${expected}")
        message(FATAL_ERROR "search index missing '${expected}'")
    endif()
endforeach()

execute_process(
    COMMAND "${TMFDEV}" verify-package "${OUTPUT_DIRECTORY}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE verify_result
    OUTPUT_VARIABLE verify_output
    ERROR_VARIABLE verify_error
)

if(NOT verify_result EQUAL 0)
    message(FATAL_ERROR "package verification failed (${verify_result}): ${verify_error}")
endif()

foreach(expected
    "scope:        whole-model"
    "integrity:    artifact hashes verified"
    "status:       verified"
)
    if(NOT verify_output MATCHES "${expected}")
        message(FATAL_ERROR "package verification output missing '${expected}':\n${verify_output}")
    endif()
endforeach()

set(tamper_readme_backup "${OUTPUT_DIRECTORY}.README.canary-backup")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${OUTPUT_DIRECTORY}/README.md"
        "${tamper_readme_backup}"
    RESULT_VARIABLE backup_readme_result
)

if(NOT backup_readme_result EQUAL 0)
    message(FATAL_ERROR "could not back up package README for tamper test")
endif()

file(APPEND "${OUTPUT_DIRECTORY}/README.md" "\npackage-canary-tamper\n")

execute_process(
    COMMAND "${TMFDEV}" verify-package "${OUTPUT_DIRECTORY}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE tampered_verify_result
    OUTPUT_VARIABLE tampered_verify_output
    ERROR_VARIABLE tampered_verify_error
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${tamper_readme_backup}"
        "${OUTPUT_DIRECTORY}/README.md"
    RESULT_VARIABLE restore_readme_result
)
file(REMOVE "${tamper_readme_backup}")

if(NOT restore_readme_result EQUAL 0)
    message(FATAL_ERROR "could not restore package README after tamper test")
endif()

if(tampered_verify_result EQUAL 0)
    message(FATAL_ERROR "tampered package unexpectedly verified")
endif()

if(NOT tampered_verify_error MATCHES "package artifact hash mismatch")
    message(FATAL_ERROR "tampered package failed for an unexpected reason: ${tampered_verify_error}\n${tampered_verify_output}")
endif()

set(tamper_runtime_backup "${OUTPUT_DIRECTORY}.runtime.canary-backup")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${OUTPUT_DIRECTORY}/include/tmfdev/runtime.hpp"
        "${tamper_runtime_backup}"
    RESULT_VARIABLE backup_runtime_result
)

if(NOT backup_runtime_result EQUAL 0)
    message(FATAL_ERROR "could not back up package runtime header for tamper test")
endif()

file(APPEND "${OUTPUT_DIRECTORY}/include/tmfdev/runtime.hpp" "\npackage-canary-runtime-tamper\n")

execute_process(
    COMMAND "${TMFDEV}" verify-package "${OUTPUT_DIRECTORY}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE tampered_runtime_verify_result
    OUTPUT_VARIABLE tampered_runtime_verify_output
    ERROR_VARIABLE tampered_runtime_verify_error
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${tamper_runtime_backup}"
        "${OUTPUT_DIRECTORY}/include/tmfdev/runtime.hpp"
    RESULT_VARIABLE restore_runtime_result
)
file(REMOVE "${tamper_runtime_backup}")

if(NOT restore_runtime_result EQUAL 0)
    message(FATAL_ERROR "could not restore package runtime header after tamper test")
endif()

if(tampered_runtime_verify_result EQUAL 0)
    message(FATAL_ERROR "tampered runtime package unexpectedly verified")
endif()

if(NOT tampered_runtime_verify_error MATCHES "package artifact hash mismatch")
    message(FATAL_ERROR "tampered runtime package failed for an unexpected reason: ${tampered_runtime_verify_error}\n${tampered_runtime_verify_output}")
endif()

execute_process(
    COMMAND "${TMFDEV}" search-package "${OUTPUT_DIRECTORY}" function PlaceBlock --limit 5
    RESULT_VARIABLE package_search_result
    OUTPUT_VARIABLE package_search_output
    ERROR_VARIABLE package_search_error
)

if(NOT package_search_result EQUAL 0)
    message(FATAL_ERROR "package search failed (${package_search_result}): ${package_search_error}")
endif()

foreach(expected
    "kind:     function"
    "query:    PlaceBlock"
    "CTrackManiaEditor::PlaceBlock"
    "physical: code:000AE110"
    "ABI:      ready"
    "SDK:      emitted"
)
    if(NOT package_search_output MATCHES "${expected}")
        message(FATAL_ERROR "package search output missing '${expected}':\n${package_search_output}")
    endif()
endforeach()

execute_process(
    COMMAND "${TMFDEV}" search-package "${OUTPUT_DIRECTORY}" function "function:CTrackManiaEditor::PlaceBlock@000AE110" --owner CTrackManiaEditor --abi ready --limit 1
    RESULT_VARIABLE filtered_search_result
    OUTPUT_VARIABLE filtered_search_output
    ERROR_VARIABLE filtered_search_error
)

if(NOT filtered_search_result EQUAL 0)
    message(FATAL_ERROR "filtered package search failed (${filtered_search_result}): ${filtered_search_error}")
endif()

foreach(expected
    "owner:    CTrackManiaEditor"
    "ABI:      ready"
    "matches:  1"
    "showing 1"
    "CTrackManiaEditor::PlaceBlock"
)
    if(NOT filtered_search_output MATCHES "${expected}")
        message(FATAL_ERROR "filtered package search output missing '${expected}':\n${filtered_search_output}")
    endif()
endforeach()

execute_process(
    COMMAND "${TMFDEV}" search-package "${OUTPUT_DIRECTORY}" function GmNat3 --owner CTrackManiaEditor --limit 1
    RESULT_VARIABLE type_reference_search_result
    OUTPUT_VARIABLE type_reference_search_output
    ERROR_VARIABLE type_reference_search_error
)

if(NOT type_reference_search_result EQUAL 0)
    message(FATAL_ERROR "package type-reference search failed (${type_reference_search_result}): ${type_reference_search_error}")
endif()

foreach(expected
    "owner:    CTrackManiaEditor"
    "parameter_types="
    "GmNat3"
)
    if(NOT type_reference_search_output MATCHES "${expected}")
        message(FATAL_ERROR "package type-reference search output missing '${expected}':\n${type_reference_search_output}")
    endif()
endforeach()

execute_process(
    COMMAND "${TMFDEV}" search-package "${OUTPUT_DIRECTORY}" function member-pointer --limit 1
    RESULT_VARIABLE diagnostic_search_result
    OUTPUT_VARIABLE diagnostic_search_output
    ERROR_VARIABLE diagnostic_search_error
)

if(NOT diagnostic_search_result EQUAL 0)
    message(FATAL_ERROR "package diagnostic search failed (${diagnostic_search_result}): ${diagnostic_search_error}")
endif()

foreach(expected
    "matches:  "
    "CClassicDependant::AddDependant"
    "ABI:      blocked"
    "SDK:      skipped"
    "member-pointer declarator ABI representation is not modeled"
)
    if(NOT diagnostic_search_output MATCHES "${expected}")
        message(FATAL_ERROR "package diagnostic search output missing '${expected}':\n${diagnostic_search_output}")
    endif()
endforeach()

set(broad_compile_project "${OUTPUT_DIRECTORY}/broad-compile-project")
set(broad_smoke_source "${broad_compile_project}/broad_sdk_smoke.cpp")
file(MAKE_DIRECTORY "${broad_compile_project}")
file(WRITE "${broad_smoke_source}" [=[
#include "TMForever_all.hpp"
#include <tmfdev/runtime.hpp>

static_assert(
    tmf::native::CGameApp_GetNetwork_00058D00Rva == 0x00058D00u
);
static_assert(tmf::native::CGameApp_VtableRva == 0x00760DFCu);

int main()
{
    const auto function =
        tmf::api::CGameAppApi::resolve_CGameApp_GetNetwork_00058D00(
            0x00400000u
        );
    return function != nullptr ? 0 : 1;
}
]=])

file(TO_CMAKE_PATH
    "${OUTPUT_DIRECTORY}/tmfdev.cmake"
    package_cmake_file
)

file(WRITE "${broad_compile_project}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.20)\n"
    "project(tmfdev_broad_sdk_smoke LANGUAGES CXX)\n"
    "set(CMAKE_CXX_STANDARD 20)\n"
    "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
    "add_executable(tmfdev_broad_sdk_smoke broad_sdk_smoke.cpp)\n"
    "include(\"${package_cmake_file}\")\n"
    "target_link_libraries(tmfdev_broad_sdk_smoke PRIVATE tmfdev::sdk tmfdev::runtime)\n"
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${broad_compile_project}" -B "${broad_compile_project}/build" -A Win32
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error
)

if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "broad header smoke project configure failed (${configure_result}):\n${configure_error}\n${configure_output}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${broad_compile_project}/build" --config Debug
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_error
)

if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR "broad generated header did not compile (${compile_result}):\n${compile_error}\n${compile_output}")
endif()

set(runtime_consumer_build "${OUTPUT_DIRECTORY}.runtime-consumer")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S
        "${CMAKE_CURRENT_LIST_DIR}/../examples/runtime_consumer"
        -B "${runtime_consumer_build}"
        -A Win32
        "-DTMFDEV_PACKAGE=${OUTPUT_DIRECTORY}"
    RESULT_VARIABLE runtime_configure_result
    OUTPUT_VARIABLE runtime_configure_output
    ERROR_VARIABLE runtime_configure_error
)

if(NOT runtime_configure_result EQUAL 0)
    message(FATAL_ERROR "runtime consumer configure failed (${runtime_configure_result}):\n${runtime_configure_error}\n${runtime_configure_output}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${runtime_consumer_build}" --config Debug
    RESULT_VARIABLE runtime_compile_result
    OUTPUT_VARIABLE runtime_compile_output
    ERROR_VARIABLE runtime_compile_error
)

if(NOT runtime_compile_result EQUAL 0)
    message(FATAL_ERROR "runtime consumer did not compile or link (${runtime_compile_result}):\n${runtime_compile_error}\n${runtime_compile_output}")
endif()

execute_process(
    COMMAND "${runtime_consumer_build}/Debug/tmforever_runtime_consumer.exe"
    RESULT_VARIABLE runtime_run_result
    OUTPUT_VARIABLE runtime_run_output
    ERROR_VARIABLE runtime_run_error
)

if(NOT runtime_run_result EQUAL 0)
    message(FATAL_ERROR "runtime consumer failed (${runtime_run_result}):\n${runtime_run_error}\n${runtime_run_output}")
endif()

message(STATUS "Package canary passed: artifacts, runtime consumer, search index, and broad header compile")
