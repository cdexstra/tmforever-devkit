if(NOT DEFINED TMFDEV OR NOT EXISTS "${TMFDEV}")
    message(FATAL_ERROR "tmfdev executable was not provided")
endif()

if(NOT DEFINED FIXTURE_EXE OR NOT EXISTS "${FIXTURE_EXE}")
    message(FATAL_ERROR "TMForever executable fixture was not provided")
endif()

if(NOT DEFINED FIXTURE_MAP OR NOT EXISTS "${FIXTURE_MAP}")
    message(FATAL_ERROR "TMForever MAP fixture was not provided")
endif()

execute_process(
    COMMAND "${TMFDEV}" search function PlaceBlock "${FIXTURE_EXE}" "${FIXTURE_MAP}" --limit 5
    RESULT_VARIABLE search_result
    OUTPUT_VARIABLE search_output
    ERROR_VARIABLE search_error
)

if(NOT search_result EQUAL 0)
    message(FATAL_ERROR "search command failed (${search_result}): ${search_error}")
endif()

foreach(expected
    "kind:     function"
    "query:    PlaceBlock"
    "CTrackManiaEditor::PlaceBlock"
    "physical: code:000AE110"
    "ABI:      ready"
    "SDK:      emitted"
)
    if(NOT search_output MATCHES "${expected}")
        message(FATAL_ERROR "search output missing '${expected}':\n${search_output}")
    endif()
endforeach()

message(STATUS "Search canary passed: PlaceBlock is discoverable with ABI and SDK status")

execute_process(
    COMMAND "${TMFDEV}" search class "class:CSceneMobil" "${FIXTURE_EXE}" "${FIXTURE_MAP}" --limit 20
    RESULT_VARIABLE class_search_result
    OUTPUT_VARIABLE class_search_output
    ERROR_VARIABLE class_search_error
)

if(NOT class_search_result EQUAL 0)
    message(FATAL_ERROR "class search command failed (${class_search_result}): ${class_search_error}")
endif()

foreach(expected
    "CSceneMobil"
    "GameBox class ID:  0x0A011000"
)
    if(NOT class_search_output MATCHES "${expected}")
        message(FATAL_ERROR "class search output missing '${expected}':\n${class_search_output}")
    endif()
endforeach()

message(STATUS "Search canary passed: exact GameBox class identity is discoverable")
