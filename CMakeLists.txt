cmake_minimum_required(VERSION 4.4)

project(
    EngCoder
    VERSION 0.9.1
    DESCRIPTION "Local C++23 adaptive AI runtime with candidate-based continual learning"
    LANGUAGES CXX)

option(THREADSPIRE_AI_BUILD_TESTS "Build EngCoder core tests" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
if(MSVC)
    # Release archives are genuinely portable and do not rely on a separately
    # installed Visual C++ runtime. Debug keeps the matching static debug CRT.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(Threads REQUIRED)

# EpochGui is vendored from the clean, backend-neutral EpochGui subtree only.
# EngCoder keeps ownership of HWND lifetime, Win32 event translation, painting,
# and presentation while EpochGui supplies deterministic layout calculations.
add_subdirectory(third_party/EpochGui _deps/EpochGui)

set(THREADSPIRE_AI_CORE_SOURCES
    src/core/ini.cpp
    src/core/json.cpp
    src/models/model_registry.cpp
    src/models/local_provider.cpp
    src/models/local_server_hub.cpp
    src/routing/task.cpp
    src/routing/calculator.cpp
    src/routing/router.cpp
    src/memory/episode_store.cpp
    src/learning/expert_registry.cpp
    src/learning/candidate_registry.cpp
    src/learning/knowledge_classifier.cpp
    src/learning/continual_learning_service.cpp
    src/learning/training_state_machine.cpp
    src/learning/atomic_promotion.cpp
    src/learning/piggyback_foundry.cpp
    src/learning/english_foundation.cpp
    src/backend/completion_client.cpp
    src/backend/agent_orchestrator.cpp
    src/backend/unified_provider_router.cpp
    src/backend/task_provider_adapters.cpp
    src/backend/media_client.cpp
    src/mcp/client.cpp
    src/long_horizon/runner.cpp
    src/runtime/backend_server.cpp
    src/runtime/backend_manager.cpp
    src/runtime/capability_pack_manager.cpp
    src/runtime/capability_pack_transaction.cpp
    src/runtime/internal_model_state.cpp
    src/runtime/storage_monitor.cpp
    src/runtime/storage_cleanup.cpp
    src/resource/resource_governor.cpp
    src/util/async_operation.cpp)

if(WIN32)
    list(APPEND THREADSPIRE_AI_CORE_SOURCES
        src/platform/windows/process.cpp
        src/platform/reset_validation.cpp
        src/platform/windows/http_client.cpp
        src/platform/windows/resource_probe.cpp)
else()
    list(APPEND THREADSPIRE_AI_CORE_SOURCES
        src/platform/posix/process.cpp
        src/platform/reset_validation.cpp
        src/platform/posix/http_client.cpp
        src/platform/posix/resource_probe.cpp)
endif()

add_library(threadspire_ai_core STATIC ${THREADSPIRE_AI_CORE_SOURCES})
add_library(threadspire::ai::core ALIAS threadspire_ai_core)

target_sources(
    threadspire_ai_core
    PUBLIC
        FILE_SET threadspire_modules
        TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/modules"
        FILES
            modules/threadspire/core.ixx
            modules/threadspire/platform.ixx
            modules/threadspire/platform.reset.ixx
            modules/threadspire/storage.ixx
            modules/threadspire/models.ixx
            modules/threadspire/providers.ixx
            modules/threadspire/providers.unified.ixx
            modules/threadspire/providers.adapters.ixx
            modules/threadspire/tasks.ixx
            modules/threadspire/scheduler.ixx
            modules/threadspire/scheduler.packs.ixx
            modules/threadspire/learning.ixx
            modules/threadspire/learning.state_machine.ixx
            modules/threadspire/learning.promotion.ixx
            modules/threadspire/media.ixx
            modules/threadspire/mcp.ixx)

target_include_directories(
    threadspire_ai_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(threadspire_ai_core PUBLIC Threads::Threads)
target_compile_features(threadspire_ai_core PUBLIC cxx_std_23)
target_compile_definitions(
    threadspire_ai_core
    PRIVATE
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN;NOMINMAX;WINVER=0x0A00;_WIN32_WINNT=0x0A00>)

if(MSVC)
    target_compile_options(
        threadspire_ai_core
        PRIVATE
            /W4 /WX /permissive- /EHsc /utf-8 /Zc:__cplusplus /Zc:preprocessor /Zc:inline /MP)
else()
    target_compile_options(
        threadspire_ai_core
        PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

if(WIN32)
    target_link_libraries(threadspire_ai_core PRIVATE Winhttp)

    enable_language(RC)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/resource.h" THREADSPIRE_RESOURCE_HEADER)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/engcoder-eagle.ico" THREADSPIRE_APP_ICON)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/models.ini" THREADSPIRE_DEFAULT_MODELS)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/seed.jsonl" THREADSPIRE_SEED_DATA)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/anchors.jsonl" THREADSPIRE_ANCHOR_DATA)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/bootstrap.jsonl" THREADSPIRE_BOOTSTRAP_DATA)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/bootstrap_anchors.jsonl" THREADSPIRE_BOOTSTRAP_ANCHORS)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/custom_training_template.jsonl" THREADSPIRE_CUSTOM_TRAINING_TEMPLATE)
    configure_file(
        resources/engcoder.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/generated/engcoder.rc"
        @ONLY)

    add_executable(
        EngCoder
        WIN32
        src/app/winmain.cpp
        src/app/app_paths.cpp
        src/app/main_window.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/engcoder.manifest"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/engcoder.rc")

    target_sources(
        EngCoder
        PRIVATE
            FILE_SET threadspire_ui_module
            TYPE CXX_MODULES
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/modules"
            FILES modules/threadspire/ui.ixx)

    target_include_directories(
        EngCoder
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(
        EngCoder
        PRIVATE
            UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(
        EngCoder
        PRIVATE
            /W4 /WX /permissive- /EHsc /utf-8 /Zc:__cplusplus /Zc:preprocessor /Zc:inline /MP)
    target_link_libraries(
        EngCoder
        PRIVATE
            threadspire::ai::core
            EpochGui
            Comctl32
            Dwmapi
            Gdi32
            Ole32
            Psapi
            Shell32
            Uuid
            UxTheme
            Winhttp)
    set_target_properties(EngCoder PROPERTIES OUTPUT_NAME "EngCoder")
    install(TARGETS EngCoder RUNTIME DESTINATION .)
    install(FILES license.txt third-party-notices.txt DESTINATION .)
    install(
        FILES third_party/EpochGui/LICENSE
        DESTINATION licenses
        RENAME EpochGui-LICENSE.txt)

    add_executable(EngCoderNativeHost src/native_host/main.cpp)
    target_link_libraries(
        EngCoderNativeHost
        PRIVATE
            threadspire::ai::core
            Bcrypt
            Ws2_32)
    target_compile_definitions(
        EngCoderNativeHost
        PRIVATE
            UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(
        EngCoderNativeHost
        PRIVATE
            /W4 /WX /permissive- /EHsc /utf-8 /Zc:__cplusplus /Zc:preprocessor /Zc:inline /MP)
    set_target_properties(
        EngCoderNativeHost
        PROPERTIES OUTPUT_NAME "engcoder-native-host")
    add_dependencies(EngCoder EngCoderNativeHost)
    add_custom_command(
        TARGET EngCoder POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:EngCoder>/optional-packs/compact-native-inference/bin"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:EngCoderNativeHost>"
            "$<TARGET_FILE_DIR:EngCoder>/optional-packs/compact-native-inference/bin/engcoder-native-host.exe"
        VERBATIM)
    install(
        TARGETS EngCoderNativeHost
        RUNTIME DESTINATION optional-packs/compact-native-inference/bin)
endif()

if(THREADSPIRE_AI_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_executable(engcoder_core_tests tests/core_tests.cpp)
    target_link_libraries(engcoder_core_tests PRIVATE threadspire::ai::core)
    target_compile_definitions(
        engcoder_core_tests
        PRIVATE
            THREADSPIRE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(
            engcoder_core_tests
            PRIVATE /W4 /WX /permissive- /EHsc /utf-8 /Zc:__cplusplus /Zc:preprocessor)
    else()
        target_compile_options(
            engcoder_core_tests
            PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    add_test(NAME engcoder_core_tests COMMAND engcoder_core_tests)

    add_executable(engcoder_workbench_tests tests/workbench_tests.cpp)
    target_link_libraries(engcoder_workbench_tests PRIVATE threadspire::ai::core)
    target_compile_definitions(
        engcoder_workbench_tests
        PRIVATE
            THREADSPIRE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(
            engcoder_workbench_tests
            PRIVATE /W4 /WX /permissive- /EHsc /utf-8 /Zc:__cplusplus /Zc:preprocessor)
    else()
        target_compile_options(
            engcoder_workbench_tests
            PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    add_test(NAME engcoder_workbench_tests COMMAND engcoder_workbench_tests)

    if(WIN32)
        add_test(
            NAME engcoder_native_inference_contract
            COMMAND "$<TARGET_FILE:EngCoderNativeHost>" --engine-contract-self-test)
        add_test(
            NAME engcoder_base_footprint
            COMMAND
                "${CMAKE_COMMAND}"
                "-DTHREADSPIRE_BASE_EXECUTABLE=$<TARGET_FILE:EngCoder>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_base_footprint.cmake")
    endif()

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME engcoder_curriculum_guard_smoke
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/curriculum_guard_smoke.py"
                --source-root "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(
            NAME engcoder_curriculum_compiler_smoke
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/curriculum_compiler_smoke.py"
                --source-root "${CMAKE_CURRENT_SOURCE_DIR}")
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -c "import torch, safetensors"
            RESULT_VARIABLE THREADSPIRE_PYTORCH_READY
            OUTPUT_QUIET
            ERROR_QUIET)
        if(THREADSPIRE_PYTORCH_READY EQUAL 0)
            add_test(
                NAME engcoder_internal_model_smoke
                COMMAND
                    "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/internal_model_smoke.py"
                    --source-root "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(
                NAME engcoder_internal_server_smoke
                COMMAND
                    "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/internal_server_smoke.py"
                    --source-root "${CMAKE_CURRENT_SOURCE_DIR}")
        else()
            message(STATUS "PyTorch/Safetensors not present; tensor smoke tests are available after installing the optional Internal PyTorch Training pack")
        endif()
    endif()
endif()
