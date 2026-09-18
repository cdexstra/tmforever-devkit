if(NOT DEFINED TMFDEV OR NOT EXISTS "${TMFDEV}")
    message(FATAL_ERROR "tmfdev executable was not provided")
endif()

if(NOT DEFINED FIXTURE_EXE OR NOT EXISTS "${FIXTURE_EXE}")
    message(FATAL_ERROR "TMForever executable fixture was not provided")
endif()

if(NOT DEFINED FIXTURE_MAP OR NOT EXISTS "${FIXTURE_MAP}")
    message(FATAL_ERROR "TMForever MAP fixture was not provided")
endif()

if(NOT DEFINED OUTPUT_MANIFEST)
    message(FATAL_ERROR "reflection manifest output path was not provided")
endif()

execute_process(
    COMMAND "${TMFDEV}" inspect-reflection CSceneMobil "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE inspect_result
    OUTPUT_VARIABLE inspect_output
    ERROR_VARIABLE inspect_error
)

if(NOT inspect_result EQUAL 0)
    message(FATAL_ERROR "reflection inspection failed (${inspect_result}): ${inspect_error}")
endif()

foreach(expected
    "idx  id          size  type"
    "0x00000005 \\(Class\\)"
    "0x00000000 \\(Action\\)"
    "flags1"
    "flags2"
    "      -1"
    "Model"
    "Show"
    "\[physical-member\]"
    "\[virtual-parameter\]"
    "\[action\]"
)
    if(NOT inspect_output MATCHES "${expected}")
        message(FATAL_ERROR "reflection inspection missing '${expected}':\n${inspect_output}")
    endif()
endforeach()

execute_process(
    COMMAND "${TMFDEV}" inspect-model CSceneMobil "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE model_result
    OUTPUT_VARIABLE model_output
    ERROR_VARIABLE model_error
)

if(NOT model_result EQUAL 0)
    message(FATAL_ERROR "model reflection inspection failed (${model_result}): ${model_error}")
endif()

if(NOT model_output MATCHES "reflection descriptors: 14")
    message(FATAL_ERROR "canonical model did not retain all CSceneMobil reflection rows:\n${model_output}")
endif()

execute_process(
    COMMAND "${TMFDEV}" inspect-reflection CGameCtnChallenge "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE specialized_result
    OUTPUT_VARIABLE specialized_output
    ERROR_VARIABLE specialized_error
)

if(NOT specialized_result EQUAL 0)
    message(FATAL_ERROR "specialized reflection inspection failed (${specialized_result}): ${specialized_error}")
endif()

foreach(expected
    "Kind.*physical-member.*enum-values=13"
    "PlayMode.*physical-member.*enum-values=6"
    "ComputeCrc32.*procedure args=2"
    "enum values:.*0="
    "procedure arguments:"
    "name=.*class-id="
)
    if(NOT specialized_output MATCHES "${expected}")
        message(FATAL_ERROR "specialized reflection inspection missing '${expected}':\n${specialized_output}")
    endif()
endforeach()

get_filename_component(manifest_directory "${OUTPUT_MANIFEST}" DIRECTORY)
file(MAKE_DIRECTORY "${manifest_directory}")

execute_process(
    COMMAND "${TMFDEV}" generate-manifest CGameCtnChallenge "${OUTPUT_MANIFEST}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE manifest_result
    OUTPUT_VARIABLE manifest_output
    ERROR_VARIABLE manifest_error
)

if(NOT manifest_result EQUAL 0)
    message(FATAL_ERROR "specialized reflection manifest failed (${manifest_result}): ${manifest_error}")
endif()

file(READ "${OUTPUT_MANIFEST}" manifest_json)
foreach(expected
    "\"reflection_descriptor_ids\""
    "\"reflection_descriptors\""
    "\"kind\":\"enum\""
    "\"enum_values\":[{\"index\":0,"
    "\"kind\":\"procedure\""
    "\"procedure_arguments\":[{\"index\":0"
)
    string(FIND "${manifest_json}" "${expected}" expected_position)
    if(expected_position EQUAL -1)
        message(FATAL_ERROR "specialized reflection manifest missing '${expected}'")
    endif()
endforeach()

message(STATUS "Reflection descriptor canary passed: typed tags, signed offsets, and flags are exposed")
