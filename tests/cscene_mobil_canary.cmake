if(NOT DEFINED TMFDEV OR NOT EXISTS "${TMFDEV}")
    message(FATAL_ERROR "tmfdev executable was not provided")
endif()

if(NOT DEFINED FIXTURE_EXE OR NOT EXISTS "${FIXTURE_EXE}")
    message(FATAL_ERROR "TMForever executable fixture was not provided")
endif()

if(NOT DEFINED FIXTURE_MAP OR NOT EXISTS "${FIXTURE_MAP}")
    message(FATAL_ERROR "TMForever MAP fixture was not provided")
endif()

if(NOT DEFINED OUTPUT_HEADER)
    message(FATAL_ERROR "canary output path was not provided")
endif()

if(NOT DEFINED OUTPUT_MANIFEST)
    message(FATAL_ERROR "canary manifest path was not provided")
endif()

get_filename_component(output_directory "${OUTPUT_HEADER}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

execute_process(
    COMMAND "${TMFDEV}" inspect-abi CSceneMobil "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE inspect_result
    OUTPUT_VARIABLE inspect_output
    ERROR_VARIABLE inspect_error
)

if(NOT inspect_result EQUAL 0)
    message(FATAL_ERROR "CSceneMobil inspect-abi failed (${inspect_result}): ${inspect_error}")
endif()

foreach(expected
    "logical functions: 141"
    "ready:             139"
    "conditional:       2"
    "blocked:           0"
)
    if(NOT inspect_output MATCHES "${expected}")
        message(FATAL_ERROR "CSceneMobil inspect-abi missing '${expected}':\n${inspect_output}")
    endif()
endforeach()

execute_process(
    COMMAND "${TMFDEV}" inspect-class CSceneMobil "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE class_result
    OUTPUT_VARIABLE class_output
    ERROR_VARIABLE class_error
)

if(NOT class_result EQUAL 0)
    message(FATAL_ERROR "CSceneMobil inspect-class failed (${class_result}): ${class_error}")
endif()

if(NOT class_output MATCHES "vtable evidence:[^\\r\\n]*0x00B9E5D4")
    message(FATAL_ERROR "CSceneMobil inspect-class did not expose the valid vtable evidence:\n${class_output}")
endif()

execute_process(
    COMMAND "${TMFDEV}" inspect-model CSceneMobil "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE model_result
    OUTPUT_VARIABLE model_output
    ERROR_VARIABLE model_error
)

if(NOT model_result EQUAL 0)
    message(FATAL_ERROR "CSceneMobil inspect-model failed (${model_result}): ${model_error}")
endif()

foreach(expected
    "address-only globals: 3"
    "ABI readiness"
    "  ready:       139"
    "  conditional: 2"
    "  blocked:     0"
    "ABI readiness \\(direct-owner\\)"
    "  ready:       107"
    "  conditional: 0"
    "  blocked:     0"
    "SDK surface"
    "  emitted:     107"
    "  skipped:     0"
)
    if(NOT model_output MATCHES "${expected}")
        message(FATAL_ERROR "CSceneMobil inspect-model missing '${expected}':\n${model_output}")
    endif()
endforeach()

execute_process(
    COMMAND "${TMFDEV}" generate-sdk CSceneMobil "${OUTPUT_HEADER}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE generate_result
    OUTPUT_VARIABLE generate_output
    ERROR_VARIABLE generate_error
)

if(NOT generate_result EQUAL 0)
    message(FATAL_ERROR "CSceneMobil generate-sdk failed (${generate_result}): ${generate_error}")
endif()

foreach(expected
    "emitted functions:[^\\r\\n]*107"
    "skipped functions:[^\\r\\n]*0"
)
    if(NOT generate_output MATCHES "${expected}")
        message(FATAL_ERROR "CSceneMobil generate-sdk missing '${expected}':\n${generate_output}")
    endif()
endforeach()

if(NOT EXISTS "${OUTPUT_HEADER}")
    message(FATAL_ERROR "CSceneMobil generator did not create ${OUTPUT_HEADER}")
endif()

file(SIZE "${OUTPUT_HEADER}" output_size)
if(output_size LESS 1000)
    message(FATAL_ERROR "CSceneMobil generated header is unexpectedly small: ${output_size} bytes")
endif()

file(READ "${OUTPUT_HEADER}" header_text)
foreach(expected
    "tmfdev generated headers target different executable builds"
    "CSceneMobil_MwClassId"
    "CSceneMobil_VtableRva"
    "resolve_CSceneMobil_Vtable"
)
    if(NOT header_text MATCHES "${expected}")
        message(FATAL_ERROR "CSceneMobil header is missing '${expected}'")
    endif()
endforeach()

execute_process(
    COMMAND "${TMFDEV}" generate-manifest CSceneMobil "${OUTPUT_MANIFEST}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE manifest_result
    OUTPUT_VARIABLE manifest_output
    ERROR_VARIABLE manifest_error
)

if(NOT manifest_result EQUAL 0)
    message(FATAL_ERROR "CSceneMobil generate-manifest failed (${manifest_result}): ${manifest_error}")
endif()

file(READ "${OUTPUT_MANIFEST}" manifest_json)
foreach(expected
    "\"schema\": 2"
    "\"requested_class\":\"CSceneMobil\""
    "\"mw_class_id\":167841792"
    "\"class_evidence\":\\[\\{\"source\":\"executable\",\"confidence\":\"verified\""
    "\"direct_function_ids\""
    "\"reflection_descriptor_ids\""
    "\"reflection_descriptors\""
    "\"category\":\"action\""
    "\"physical_storage\":false"
    "\"rtti_hierarchy\""
    "\"vtable\":{\"state\":\"valid\""
    "\"abi_issues\""
    "\"signature\":{\"return_type\""
    "\"parameters\""
)
    if(NOT manifest_json MATCHES "${expected}")
        message(FATAL_ERROR "CSceneMobil manifest missing '${expected}'")
    endif()
endforeach()

set(class_package_directory "${output_directory}/CSceneMobil-package")
file(REMOVE_RECURSE "${class_package_directory}")

execute_process(
    COMMAND "${TMFDEV}" generate-class-package CSceneMobil "${class_package_directory}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE package_result
    OUTPUT_VARIABLE package_output
    ERROR_VARIABLE package_error
)

if(NOT package_result EQUAL 0)
    message(FATAL_ERROR "CSceneMobil class package generation failed (${package_result}): ${package_error}")
endif()

foreach(expected_file
    "${class_package_directory}/README.md"
    "${class_package_directory}/package.json"
    "${class_package_directory}/tmfdev.cmake"
    "${class_package_directory}/include/TMForever.hpp"
    "${class_package_directory}/include/tmfdev/CSceneMobil.hpp"
    "${class_package_directory}/metadata/TMForever_manifest.json"
    "${class_package_directory}/metadata/search-index.tsv"
)
    if(NOT EXISTS "${expected_file}")
        message(FATAL_ERROR "CSceneMobil class package is missing ${expected_file}")
    endif()
endforeach()

file(READ "${class_package_directory}/package.json" class_package_json)
foreach(expected
    "\"requested_class\": \"CSceneMobil\""
    "\"sdk\": \"include/TMForever.hpp\""
    "\"class_sdk\": \"include/tmfdev/CSceneMobil.hpp\""
    "\"cmake\": \"tmfdev.cmake\""
    "\"emitted_functions\": 107"
    "\"skipped_functions\": 0"
)
    if(NOT class_package_json MATCHES "${expected}")
        message(FATAL_ERROR "CSceneMobil class package metadata missing '${expected}'")
    endif()
endforeach()

execute_process(
    COMMAND "${TMFDEV}" verify-package "${class_package_directory}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE verify_package_result
    OUTPUT_VARIABLE verify_package_output
    ERROR_VARIABLE verify_package_error
)

if(NOT verify_package_result EQUAL 0)
    message(FATAL_ERROR "CSceneMobil class package verification failed (${verify_package_result}): ${verify_package_error}")
endif()

if(NOT verify_package_output MATCHES "status:       verified")
    message(FATAL_ERROR "CSceneMobil class package verification did not pass:\n${verify_package_output}")
endif()

set(tamper_class_include_backup "${class_package_directory}.include.canary-backup")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${class_package_directory}/include/tmfdev/CSceneMobil.hpp"
        "${tamper_class_include_backup}"
    RESULT_VARIABLE backup_class_include_result
)

if(NOT backup_class_include_result EQUAL 0)
    message(FATAL_ERROR "could not back up class include for tamper test")
endif()

file(APPEND "${class_package_directory}/include/tmfdev/CSceneMobil.hpp" "\nclass-package-canary-tamper\n")

execute_process(
    COMMAND "${TMFDEV}" verify-package "${class_package_directory}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE tampered_class_verify_result
    OUTPUT_VARIABLE tampered_class_verify_output
    ERROR_VARIABLE tampered_class_verify_error
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${tamper_class_include_backup}"
        "${class_package_directory}/include/tmfdev/CSceneMobil.hpp"
    RESULT_VARIABLE restore_class_include_result
)
file(REMOVE "${tamper_class_include_backup}")

if(NOT restore_class_include_result EQUAL 0)
    message(FATAL_ERROR "could not restore class include after tamper test")
endif()

if(tampered_class_verify_result EQUAL 0)
    message(FATAL_ERROR "tampered CSceneMobil class include unexpectedly verified")
endif()

if(NOT tampered_class_verify_error MATCHES "package artifact hash mismatch")
    message(FATAL_ERROR "tampered CSceneMobil class include failed for an unexpected reason: ${tampered_class_verify_error}\n${tampered_class_verify_output}")
endif()

set(class_include_project "${class_package_directory}/class-include-project")
file(MAKE_DIRECTORY "${class_include_project}")
file(TO_CMAKE_PATH
    "${CMAKE_CURRENT_LIST_DIR}/../examples/package_consumer"
    package_consumer_source_directory
)

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${package_consumer_source_directory}"
        -B "${class_include_project}/build"
        "-DTMFDEV_PACKAGE=${class_package_directory}"
    RESULT_VARIABLE class_include_configure_result
    OUTPUT_VARIABLE class_include_configure_output
    ERROR_VARIABLE class_include_configure_error
)

if(NOT class_include_configure_result EQUAL 0)
    message(FATAL_ERROR "CSceneMobil class include project configure failed (${class_include_configure_result}):\n${class_include_configure_error}\n${class_include_configure_output}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${class_include_project}/build" --config Debug
    RESULT_VARIABLE class_include_compile_result
    OUTPUT_VARIABLE class_include_compile_output
    ERROR_VARIABLE class_include_compile_error
)

if(NOT class_include_compile_result EQUAL 0)
    message(FATAL_ERROR "CSceneMobil class include did not compile (${class_include_compile_result}):\n${class_include_compile_error}\n${class_include_compile_output}")
endif()

message(STATUS "CSceneMobil canary passed: 141 logical, 139 ready / 2 conditional, 107 emitted / 0 skipped; manifest schema 2")
