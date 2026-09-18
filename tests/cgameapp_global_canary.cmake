if(NOT DEFINED TMFDEV OR NOT EXISTS "${TMFDEV}")
    message(FATAL_ERROR "tmfdev executable was not provided")
endif()

if(NOT DEFINED FIXTURE_EXE OR NOT EXISTS "${FIXTURE_EXE}")
    message(FATAL_ERROR "TMForever executable fixture was not provided")
endif()

if(NOT DEFINED FIXTURE_MAP OR NOT EXISTS "${FIXTURE_MAP}")
    message(FATAL_ERROR "TMForever MAP fixture was not provided")
endif()

if(NOT DEFINED OUTPUT_HEADER OR NOT DEFINED OUTPUT_MANIFEST)
    message(FATAL_ERROR "CGameApp output paths were not provided")
endif()

get_filename_component(output_directory "${OUTPUT_HEADER}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

execute_process(
    COMMAND "${TMFDEV}" generate-sdk CGameApp "${OUTPUT_HEADER}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE generate_result
    OUTPUT_VARIABLE generate_output
    ERROR_VARIABLE generate_error
)

if(NOT generate_result EQUAL 0)
    message(FATAL_ERROR "CGameApp generate-sdk failed (${generate_result}): ${generate_error}")
endif()

if(NOT EXISTS "${OUTPUT_HEADER}")
    message(FATAL_ERROR "CGameApp generator did not create ${OUTPUT_HEADER}")
endif()

file(READ "${OUTPUT_HEADER}" header_text)
foreach(expected
    "Global_00968C44_public__static_class_CGameApp___CGameApp__s_TheGameRva"
    "resolve_Global_00968C44_public__static_class_CGameApp___CGameApp__s_TheGame"
    "Global_00968C50_public__static_class_CMwClassInfo_CGameApp__m_MwClassInfo_CGameAppRva"
)
    if(NOT header_text MATCHES "${expected}")
        message(FATAL_ERROR "CGameApp header missing '${expected}'")
    endif()
endforeach()

if(header_text MATCHES "Global_00968C44_public__static_class_CGameApp___CGameApp__s_TheGame_[0-9]+Rva")
    message(FATAL_ERROR "CGameApp global resolver still has an unstable numeric suffix")
endif()

execute_process(
    COMMAND "${TMFDEV}" generate-manifest CGameApp "${OUTPUT_MANIFEST}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE manifest_result
    OUTPUT_VARIABLE manifest_output
    ERROR_VARIABLE manifest_error
)

if(NOT manifest_result EQUAL 0)
    message(FATAL_ERROR "CGameApp generate-manifest failed (${manifest_result}): ${manifest_error}")
endif()

file(READ "${OUTPUT_MANIFEST}" manifest_json)
foreach(expected
    "\"schema\": 2"
    "\"requested_class\":\"CGameApp\""
    "\"mw_class_id\":50352128"
    "\"class_evidence\":\\[\\{\"source\":\"executable\",\"confidence\":\"verified\""
    "\"model_globals\": 2"
    "\"emitted_functions\": 111"
    "\"skipped_functions\": 7"
    "\"name\":\"public: static class CGameApp"
    "CGameApp::s_TheGame"
    "CGameApp::m_MwClassInfo_CGameApp"
)
    if(NOT manifest_json MATCHES "${expected}")
        message(FATAL_ERROR "CGameApp manifest missing '${expected}'")
    endif()
endforeach()

message(STATUS "CGameApp global canary passed: stable address-only global resolvers and class-scoped manifest")
