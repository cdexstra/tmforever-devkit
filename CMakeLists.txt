cmake_minimum_required(VERSION 3.20)

project(tmforever_devkit LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(tmfdev
    src/main.cpp
    src/abi.cpp
    src/sdk.cpp
    src/pe.cpp
    src/map.cpp
    src/build.cpp
    src/class.cpp
    src/demangle.cpp
    src/vtable.cpp
    src/rtti.cpp
    src/reflection.cpp
    src/model.cpp
    src/model_builder.cpp
    src/signature.cpp
)

target_link_libraries(tmfdev PRIVATE
    bcrypt
    dbghelp
)

add_executable(sdk_smoke
    generated/sdk_smoke.cpp
)

target_include_directories(sdk_smoke PRIVATE
    generated
)

add_executable(sdk_editor_consumer
    examples/editor_consumer.cpp
)

target_include_directories(sdk_editor_consumer PRIVATE
    generated
)

add_executable(sdk_input_consumer
    examples/input_consumer.cpp
)

target_include_directories(sdk_input_consumer PRIVATE
    generated
)

add_executable(sdk_audio_consumer
    examples/audio_consumer.cpp
)

target_include_directories(sdk_audio_consumer PRIVATE
    generated
)

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/TMForever_all.hpp")
    add_executable(sdk_runtime_consumer
        examples/runtime_consumer/consumer.cpp
        generated/tmfdev_runtime.cpp
    )

    target_include_directories(sdk_runtime_consumer PRIVATE
        generated
    )

    target_link_libraries(sdk_runtime_consumer PRIVATE
        bcrypt
    )
endif()

add_executable(abi_model_tests
    tests/abi_model_tests.cpp
    src/abi.cpp
    src/model.cpp
    src/sdk.cpp
)

target_include_directories(abi_model_tests PRIVATE
    src
)

if(BUILD_TESTING)
    add_test(
        NAME sdk_smoke
        COMMAND $<TARGET_FILE:sdk_smoke>
    )

    add_test(
        NAME sdk_editor_consumer
        COMMAND $<TARGET_FILE:sdk_editor_consumer>
    )

    add_test(
        NAME sdk_input_consumer
        COMMAND $<TARGET_FILE:sdk_input_consumer>
    )

    add_test(
        NAME sdk_audio_consumer
        COMMAND $<TARGET_FILE:sdk_audio_consumer>
    )

    if(TARGET sdk_runtime_consumer)
        add_test(
            NAME sdk_runtime_consumer
            COMMAND $<TARGET_FILE:sdk_runtime_consumer>
        )
    endif()

    add_test(
        NAME abi_model_tests
        COMMAND $<TARGET_FILE:abi_model_tests>
    )

    add_test(
        NAME proof_headers_canary
        COMMAND ${CMAKE_COMMAND}
            -DGENERATED_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/generated
            -DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/proof-headers
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/proof_headers_canary.cmake
    )

    set(TMFDEV_LOCAL_EXE "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/local/TmForever.exe")
    set(TMFDEV_LOCAL_MAP "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/local/TmForever.map")

    if(EXISTS "${TMFDEV_LOCAL_EXE}" AND EXISTS "${TMFDEV_LOCAL_MAP}")
        add_test(
            NAME cscene_mobil_canary
            COMMAND ${CMAKE_COMMAND}
                -DTMFDEV=$<TARGET_FILE:tmfdev>
                -DFIXTURE_EXE=${TMFDEV_LOCAL_EXE}
                -DFIXTURE_MAP=${TMFDEV_LOCAL_MAP}
                -DOUTPUT_HEADER=${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/CSceneMobil.hpp
                -DOUTPUT_MANIFEST=${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/CSceneMobil.json
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cscene_mobil_canary.cmake
        )

        add_test(
            NAME cgameapp_global_canary
            COMMAND ${CMAKE_COMMAND}
                -DTMFDEV=$<TARGET_FILE:tmfdev>
                -DFIXTURE_EXE=${TMFDEV_LOCAL_EXE}
                -DFIXTURE_MAP=${TMFDEV_LOCAL_MAP}
                -DOUTPUT_HEADER=${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/CGameApp.hpp
                -DOUTPUT_MANIFEST=${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/CGameApp.json
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cgameapp_global_canary.cmake
        )

        add_test(
            NAME mw_class_id_fallback_canary
            COMMAND ${CMAKE_COMMAND}
                -DTMFDEV=$<TARGET_FILE:tmfdev>
                -DFIXTURE_EXE=${TMFDEV_LOCAL_EXE}
                -DFIXTURE_MAP=${TMFDEV_LOCAL_MAP}
                -DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/mw-class-id-fallback
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/mw_class_id_fallback_canary.cmake
        )

        add_test(
            NAME cmw_nod_safety_canary
            COMMAND ${CMAKE_COMMAND}
                -DTMFDEV=$<TARGET_FILE:tmfdev>
                -DFIXTURE_EXE=${TMFDEV_LOCAL_EXE}
                -DFIXTURE_MAP=${TMFDEV_LOCAL_MAP}
                -DOUTPUT_HEADER=${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/CMwNod.hpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmw_nod_safety_canary.cmake
        )

        add_test(
            NAME reflection_descriptor_canary
            COMMAND ${CMAKE_COMMAND}
                -DTMFDEV=$<TARGET_FILE:tmfdev>
                -DFIXTURE_EXE=${TMFDEV_LOCAL_EXE}
                -DFIXTURE_MAP=${TMFDEV_LOCAL_MAP}
                -DOUTPUT_MANIFEST=${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/CGameCtnChallenge-reflection.json
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/reflection_descriptor_canary.cmake
        )

        add_test(
            NAME search_canary
            COMMAND ${CMAKE_COMMAND}
                -DTMFDEV=$<TARGET_FILE:tmfdev>
                -DFIXTURE_EXE=${TMFDEV_LOCAL_EXE}
                -DFIXTURE_MAP=${TMFDEV_LOCAL_MAP}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/search_canary.cmake
        )

        add_test(
            NAME package_canary
            COMMAND ${CMAKE_COMMAND}
                -DTMFDEV=$<TARGET_FILE:tmfdev>
                -DFIXTURE_EXE=${TMFDEV_LOCAL_EXE}
                -DFIXTURE_MAP=${TMFDEV_LOCAL_MAP}
                -DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/TMForever-package
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_canary.cmake
        )
    endif()
endif()
