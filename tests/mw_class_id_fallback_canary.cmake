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
    message(FATAL_ERROR "fallback class-ID output directory was not provided")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")

set(classes
    CGameCalendar
    CGameCtnMediaBlockEditor
    CGameRaceInterface
    CCurveInterface
)
set(ids
    51130368
    50544640
    50888704
    84131840
)

list(LENGTH classes class_count)
math(EXPR last_index "${class_count} - 1")

foreach(index RANGE ${last_index})
    list(GET classes ${index} class_name)
    list(GET ids ${index} expected_id)
    set(output_manifest "${OUTPUT_DIRECTORY}/${class_name}.json")

    execute_process(
        COMMAND "${TMFDEV}" generate-manifest "${class_name}" "${output_manifest}" "${FIXTURE_EXE}" "${FIXTURE_MAP}"
        RESULT_VARIABLE generate_result
        OUTPUT_VARIABLE generate_output
        ERROR_VARIABLE generate_error
    )

    if(NOT generate_result EQUAL 0)
        message(FATAL_ERROR "${class_name} manifest generation failed (${generate_result}): ${generate_error}")
    endif()

    file(READ "${output_manifest}" manifest_json)
    if(NOT manifest_json MATCHES "\"mw_class_id\":${expected_id}")
        message(FATAL_ERROR "${class_name} fallback class ID ${expected_id} was not emitted")
    endif()

    if(NOT manifest_json MATCHES "\"source\":\"executable\",\"confidence\":\"verified\"")
        message(FATAL_ERROR "${class_name} fallback class ID lacks executable verification evidence")
    endif()
endforeach()

message(STATUS "Fallback GameBox class-ID canary passed for four initializer-only classes")
