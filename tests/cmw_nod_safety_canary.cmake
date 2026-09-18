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
    message(FATAL_ERROR "CMwNod output path was not provided")
endif()

get_filename_component(output_directory "${OUTPUT_HEADER}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

execute_process(
    COMMAND "${TMFDEV}" generate-sdk CMwNod "${OUTPUT_HEADER}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
    RESULT_VARIABLE generate_result
    OUTPUT_VARIABLE generate_output
    ERROR_VARIABLE generate_error
)

if(NOT generate_result EQUAL 0)
    message(FATAL_ERROR "CMwNod SDK generation failed (${generate_result}): ${generate_error}")
endif()

file(READ "${OUTPUT_HEADER}" header_text)

foreach(expected
    "CMwNod_CreateByMwClassId_00523D30Fn"
    "resolve_CMwNod_CreateByMwClassId_00523D30"
    "CMwNod_StaticGetClassInfo_00523D50Fn"
    "static CMwClassInfo const \\* StaticGetClassInfo\\("
)
    if(NOT header_text MATCHES "${expected}")
        message(FATAL_ERROR "CMwNod header is missing low-level or safe lookup surface '${expected}'")
    endif()
endforeach()

foreach(forbidden
    "static CMwNod \\* CreateByMwClassId\\("
    "static CMwNod \\* MwNew\\("
    "static void AddClass\\("
    "static void MwBuildClassInfoTree\\("
)
    if(header_text MATCHES "${forbidden}")
        message(FATAL_ERROR "CMwNod header exposed unsafe convenience facade '${forbidden}'")
    endif()
endforeach()

message(STATUS "CMwNod safety canary passed: registry/factory operations remain resolver-only")
