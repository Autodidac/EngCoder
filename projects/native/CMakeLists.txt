cmake_minimum_required(VERSION 3.28)
project(EngCoder VERSION 0.7.4 LANGUAGES CXX)

option(ENGCODER_BUILD_TESTS "Build EngCoder tests" ON)
option(ENGCODER_BUILD_GUI "Build native EngCoder GUI" ON)
option(ENGCODER_ENABLE_SANITIZERS "Enable sanitizers on supported non-MSVC builds" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(EngCoderWarnings)
include(EngCoderSanitizers)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_library(engcoder_client STATIC
    src/client/agent_client.cpp
)
add_library(engcoder::client ALIAS engcoder_client)
target_include_directories(engcoder_client
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(engcoder_client
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE httplib::httplib
)
target_compile_definitions(engcoder_client PRIVATE
    $<$<PLATFORM_ID:Windows>:NOMINMAX;WIN32_LEAN_AND_MEAN>
)
engcoder_set_warnings(engcoder_client)
engcoder_enable_sanitizers(engcoder_client)

add_library(engcoder_core STATIC
    src/core/config.cpp
    src/core/error.cpp
    src/core/event_bus.cpp
    src/core/ids.cpp
    src/core/file_replace_windows.cpp
    src/core/file_replace_posix.cpp
    src/model/lm_studio_provider.cpp
    src/model/runtime_model.cpp
    src/model/qwen_flash_runtime.cpp
    src/model/native_model_manager.cpp
    src/model/internal_model_host.cpp
    src/model/multiplex_provider.cpp
    src/models/model_registry.cpp
    src/residency/model_residency_manager.cpp
    src/addons/addon_registry.cpp
    src/controller/architect_controller.cpp
    src/controller/task_intent.cpp
    src/learning/learning_service.cpp
    src/memory/episode_store.cpp
    src/training/training_service.cpp
    src/decision/decision_engine.cpp
    src/workspace/workspace.cpp
    src/workspace/checkpoint_store.cpp
    src/process/process.cpp
    src/process/process_windows.cpp
    src/process/process_posix.cpp
    src/tools/tool_registry.cpp
    src/tools/builtin_tools.cpp
    src/persistence/session_store.cpp
    src/agent/context_manager.cpp
    src/agent/handoff_contract.cpp
    src/agent/agent_runtime.cpp
    src/agent/task_scheduler.cpp
)
add_library(engcoder::core ALIAS engcoder_core)
target_include_directories(engcoder_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_compile_definitions(engcoder_core PRIVATE
    $<$<PLATFORM_ID:Windows>:NOMINMAX;WIN32_LEAN_AND_MEAN>
)
target_link_libraries(engcoder_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE httplib::httplib CURL::libcurl unofficial::sqlite3::sqlite3
)
engcoder_set_warnings(engcoder_core)
engcoder_enable_sanitizers(engcoder_core)

add_library(engcoder_runtime STATIC
    src/runtime/runtime_host.cpp
    src/server/http_server.cpp
)
add_library(engcoder::runtime ALIAS engcoder_runtime)
target_include_directories(engcoder_runtime
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_compile_definitions(engcoder_runtime PRIVATE
    "ENGCODER_VERSION_STRING=\"${PROJECT_VERSION}\""
    $<$<PLATFORM_ID:Windows>:NOMINMAX;WIN32_LEAN_AND_MEAN>
)
target_link_libraries(engcoder_runtime
    PUBLIC engcoder::core
    PRIVATE httplib::httplib
)
engcoder_set_warnings(engcoder_runtime)
engcoder_enable_sanitizers(engcoder_runtime)

add_executable(engcoder_server apps/server/main.cpp)
set_target_properties(engcoder_server PROPERTIES OUTPUT_NAME "EngCoderServer")
target_link_libraries(engcoder_server PRIVATE engcoder::runtime)
engcoder_set_warnings(engcoder_server)
engcoder_enable_sanitizers(engcoder_server)

add_executable(engcoder_cli apps/cli/main.cpp)
set_target_properties(engcoder_cli PROPERTIES OUTPUT_NAME "EngCoderCLI")
target_link_libraries(engcoder_cli PRIVATE engcoder::runtime)
engcoder_set_warnings(engcoder_cli)
engcoder_enable_sanitizers(engcoder_cli)

if(ENGCODER_BUILD_GUI)
    find_package(SDL3 CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)

    add_executable(engcoder_gui apps/gui/main.cpp)
    set_target_properties(engcoder_gui PROPERTIES OUTPUT_NAME "EngCoder")
    target_link_libraries(engcoder_gui
        PRIVATE
            engcoder::runtime
            SDL3::SDL3
            imgui::imgui
    )
    if(WIN32)
        set_target_properties(engcoder_gui PROPERTIES WIN32_EXECUTABLE TRUE)
        target_link_libraries(engcoder_gui PRIVATE Advapi32)
    endif()
    engcoder_set_warnings(engcoder_gui)
    engcoder_enable_sanitizers(engcoder_gui)
endif()

set_target_properties(engcoder_core engcoder_client engcoder_runtime engcoder_server engcoder_cli PROPERTIES
    FOLDER "EngCoder"
)
if(TARGET engcoder_gui)
    set_target_properties(engcoder_gui PROPERTIES FOLDER "EngCoder")
endif()

if(ENGCODER_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    add_executable(engcoder_tests
        tests/test_workspace.cpp
        tests/test_context_manager.cpp
        tests/test_decision_engine.cpp
        tests/test_agent_runtime.cpp
        tests/test_handoff_contract.cpp
        tests/test_addon_registry.cpp
        tests/test_task_intent.cpp
        tests/test_learning_service.cpp
        tests/test_lm_studio_provider.cpp
        tests/test_qwen_output_journal.cpp
        tests/test_native_model_manager.cpp
        tests/test_process.cpp
    )
    target_link_libraries(engcoder_tests PRIVATE engcoder::core Catch2::Catch2WithMain)
    engcoder_set_warnings(engcoder_tests)
    engcoder_enable_sanitizers(engcoder_tests)
    include(CTest)
    include(Catch)
    # Every discovered unit test is bounded. A broken local HTTP test or a
    # future deadlock must fail the build instead of wedging auto.bat forever.
    catch_discover_tests(engcoder_tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 30
    )
    set_target_properties(engcoder_tests PROPERTIES FOLDER "Tests")
    find_package(Python3 3.8 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME boundary_lab_routing
            COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_boundary_lab.py")
        set_tests_properties(boundary_lab_routing PROPERTIES TIMEOUT 30)
    endif()
endif()

set(_engcoder_install_targets engcoder_core engcoder_client engcoder_runtime engcoder_server engcoder_cli)
if(TARGET engcoder_gui)
    list(APPEND _engcoder_install_targets engcoder_gui)
endif()
install(TARGETS ${_engcoder_install_targets}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY resources/training/ DESTINATION share/engcoder/training)
