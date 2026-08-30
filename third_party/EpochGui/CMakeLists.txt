cmake_minimum_required(VERSION 4.4)

set(CMAKE_CXX_SCAN_FOR_MODULES ON)

project(EpochGui VERSION 0.89.30 LANGUAGES CXX)

option(EPOCHGUI_ENABLE_ROUNDED_RECT "Build the optional EpochGui rounded-rectangle geometry module." OFF)

set(EPOCHGUI_SOURCE_ROOT "" CACHE PATH "Directory containing include/gui and src/epochgui.")

set(_epochgui_source_root_candidates)
if(EPOCHGUI_SOURCE_ROOT)
    list(APPEND _epochgui_source_root_candidates "${EPOCHGUI_SOURCE_ROOT}")
endif()

list(APPEND _epochgui_source_root_candidates
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

set(_epochgui_resolved_source_root "")
foreach(_epochgui_candidate IN LISTS _epochgui_source_root_candidates)
    get_filename_component(_epochgui_candidate_abs "${_epochgui_candidate}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(EXISTS "${_epochgui_candidate_abs}/include/gui/floating_window.hpp"
        AND EXISTS "${_epochgui_candidate_abs}/src/epochgui/floating_window.cpp")
        set(_epochgui_resolved_source_root "${_epochgui_candidate_abs}")
        break()
    endif()
endforeach()

if(NOT _epochgui_resolved_source_root)
    message(FATAL_ERROR
        "EpochGui could not find its source root. Expected include/gui and "
        "src/epochgui either beside this CMakeLists.txt or two directories above it. "
        "Pass -DEPOCHGUI_SOURCE_ROOT=<path> to select another source root.")
endif()

set(EPOCHGUI_SOURCE_ROOT "${_epochgui_resolved_source_root}" CACHE PATH "Directory containing include/gui and src/epochgui." FORCE)
set(EPOCHGUI_MODULE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Directory containing EpochGui module interfaces." FORCE)

set(EPOCHGUI_MODULES
    "${EPOCHGUI_MODULE_ROOT}/modules/epoch.gui.ixx"
    "${EPOCHGUI_MODULE_ROOT}/modules/epoch.gui.font.ixx"
    "${EPOCHGUI_MODULE_ROOT}/modules/epoch.gui.image.ixx"
    "${EPOCHGUI_MODULE_ROOT}/modules/epoch.gui.tile_workspace.ixx"
    "${EPOCHGUI_MODULE_ROOT}/modules/epoch.gui.input.ixx"
)

set(EPOCHGUI_PUBLIC_HEADERS
    "${EPOCHGUI_SOURCE_ROOT}/include/gui/dockable_window.hpp"
    "${EPOCHGUI_SOURCE_ROOT}/include/gui/dock_layout.hpp"
    "${EPOCHGUI_SOURCE_ROOT}/include/gui/floating_window.hpp"
    "${EPOCHGUI_SOURCE_ROOT}/include/gui/font.hpp"
    "${EPOCHGUI_SOURCE_ROOT}/include/gui/hierarchy_tree.hpp"
    "${EPOCHGUI_SOURCE_ROOT}/include/gui/layout_primitives.hpp"
    "${EPOCHGUI_SOURCE_ROOT}/include/gui/node_graph_workspace.hpp"
    "${EPOCHGUI_SOURCE_ROOT}/include/gui/panel_host.hpp"
    "${EPOCHGUI_SOURCE_ROOT}/include/gui/popup_layout.hpp"
    "${EPOCHGUI_SOURCE_ROOT}/include/gui/system_workspace.hpp"
    "${EPOCHGUI_SOURCE_ROOT}/include/gui/text_control.hpp"
    "${EPOCHGUI_SOURCE_ROOT}/include/gui/text_editor.hpp"
    "${EPOCHGUI_SOURCE_ROOT}/include/gui/version.hpp"
)

set(EPOCHGUI_SOURCES
    "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/dockable_window.cpp"
    "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/dock_layout.cpp"
    "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/floating_window.cpp"
    "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/hierarchy_tree.cpp"
    "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/image.cpp"
    "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/input.cpp"
    "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/node_graph_workspace.cpp"
    "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/panel_host.cpp"
    "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/popup_layout.cpp"
    "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/system_workspace.cpp"
    "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/text_control.cpp"
    "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/text_editor.cpp"
    "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/tile_workspace.cpp"
)

if(EPOCHGUI_ENABLE_ROUNDED_RECT)
    list(APPEND EPOCHGUI_MODULES
        "${EPOCHGUI_MODULE_ROOT}/modules/epoch.gui.rounded_rect.ixx"
    )
    list(APPEND EPOCHGUI_SOURCES
        "${EPOCHGUI_SOURCE_ROOT}/src/epochgui/rounded_rect.cpp"
    )
endif()

add_library(EpochGui STATIC
    ${EPOCHGUI_PUBLIC_HEADERS}
    ${EPOCHGUI_SOURCES}
)

target_sources(EpochGui PUBLIC
    FILE_SET epochgui_modules TYPE CXX_MODULES
        BASE_DIRS "${EPOCHGUI_MODULE_ROOT}/modules"
        FILES ${EPOCHGUI_MODULES}
)

add_library(epoch_gui ALIAS EpochGui)
add_library(Autodidac::EpochGui ALIAS EpochGui)

target_include_directories(EpochGui PUBLIC
    "$<BUILD_INTERFACE:${EPOCHGUI_SOURCE_ROOT}/include>"
)

target_compile_definitions(EpochGui PUBLIC
    EPOCHGUI_ENABLE_ROUNDED_RECT=$<BOOL:${EPOCHGUI_ENABLE_ROUNDED_RECT}>
    EPOCHGUI_ENABLE_INPUT=1
)

target_compile_features(EpochGui PUBLIC cxx_std_23)

set_target_properties(EpochGui PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    EXPORT_NAME EpochGui
    OUTPUT_NAME EpochGui
)

if(MSVC)
    target_compile_options(EpochGui PRIVATE /permissive-)
endif()

source_group(TREE "${EPOCHGUI_SOURCE_ROOT}/include" PREFIX "Header Files" FILES ${EPOCHGUI_PUBLIC_HEADERS})
source_group(TREE "${EPOCHGUI_SOURCE_ROOT}/src" PREFIX "Source Files" FILES ${EPOCHGUI_SOURCES})
source_group(TREE "${EPOCHGUI_MODULE_ROOT}/modules" PREFIX "Module Files" FILES ${EPOCHGUI_MODULES})

include(CTest)
if(BUILD_TESTING)
    add_executable(EpochGuiDockLayoutTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/dock_layout_tests.cpp"
    )
    target_link_libraries(EpochGuiDockLayoutTests PRIVATE EpochGui)
    target_compile_features(EpochGuiDockLayoutTests PRIVATE cxx_std_23)
    set_target_properties(EpochGuiDockLayoutTests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    add_test(NAME EpochGui.DockLayout COMMAND EpochGuiDockLayoutTests)

    add_executable(EpochGuiTextControlTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/text_control_tests.cpp"
    )
    target_link_libraries(EpochGuiTextControlTests PRIVATE EpochGui)
    target_compile_features(EpochGuiTextControlTests PRIVATE cxx_std_23)
    set_target_properties(EpochGuiTextControlTests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    add_test(NAME EpochGui.TextControl COMMAND EpochGuiTextControlTests)

    add_executable(EpochGuiFontTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/font_tests.cpp"
    )
    target_link_libraries(EpochGuiFontTests PRIVATE EpochGui)
    target_compile_features(EpochGuiFontTests PRIVATE cxx_std_23)
    set_target_properties(EpochGuiFontTests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    add_test(NAME EpochGui.Font COMMAND EpochGuiFontTests)

    add_executable(EpochGuiImageTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/image_tests.cpp"
    )
    target_link_libraries(EpochGuiImageTests PRIVATE EpochGui)
    target_compile_features(EpochGuiImageTests PRIVATE cxx_std_23)
    set_target_properties(EpochGuiImageTests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    add_test(NAME EpochGui.Image COMMAND EpochGuiImageTests)

    add_executable(EpochGuiTileWorkspaceTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tile_workspace_tests.cpp"
    )
    target_link_libraries(EpochGuiTileWorkspaceTests PRIVATE EpochGui)
    target_compile_features(EpochGuiTileWorkspaceTests PRIVATE cxx_std_23)
    set_target_properties(EpochGuiTileWorkspaceTests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    add_test(NAME EpochGui.TileWorkspace COMMAND EpochGuiTileWorkspaceTests)


    add_executable(EpochGuiSystemWorkspaceTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system_workspace_tests.cpp"
    )
    target_link_libraries(EpochGuiSystemWorkspaceTests PRIVATE EpochGui)
    target_compile_features(EpochGuiSystemWorkspaceTests PRIVATE cxx_std_23)
    set_target_properties(EpochGuiSystemWorkspaceTests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    add_test(NAME EpochGui.SystemWorkspace COMMAND EpochGuiSystemWorkspaceTests)

    add_executable(EpochGuiNodeGraphWorkspaceTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/node_graph_workspace_tests.cpp"
    )
    target_link_libraries(EpochGuiNodeGraphWorkspaceTests PRIVATE EpochGui)
    target_compile_features(EpochGuiNodeGraphWorkspaceTests PRIVATE cxx_std_23)
    set_target_properties(EpochGuiNodeGraphWorkspaceTests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    add_test(NAME EpochGui.NodeGraphWorkspace COMMAND EpochGuiNodeGraphWorkspaceTests)

    add_executable(EpochGuiAssetGridTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/asset_grid_tests.cpp"
    )
    target_link_libraries(EpochGuiAssetGridTests PRIVATE EpochGui)
    target_compile_features(EpochGuiAssetGridTests PRIVATE cxx_std_23)
    set_target_properties(EpochGuiAssetGridTests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    add_test(NAME EpochGui.AssetGrid COMMAND EpochGuiAssetGridTests)

    add_executable(EpochGuiHierarchyTreeTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/hierarchy_tree_tests.cpp"
    )
    target_link_libraries(EpochGuiHierarchyTreeTests PRIVATE EpochGui)
    target_compile_features(EpochGuiHierarchyTreeTests PRIVATE cxx_std_23)
    set_target_properties(EpochGuiHierarchyTreeTests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    add_test(NAME EpochGui.HierarchyTree COMMAND EpochGuiHierarchyTreeTests)

    if(EPOCHGUI_ENABLE_ROUNDED_RECT)
        add_executable(EpochGuiRoundedRectTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/rounded_rect_tests.cpp"
        )
        target_link_libraries(EpochGuiRoundedRectTests PRIVATE EpochGui)
        target_compile_features(EpochGuiRoundedRectTests PRIVATE cxx_std_23)
        set_target_properties(EpochGuiRoundedRectTests PROPERTIES
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        add_test(NAME EpochGui.RoundedRect COMMAND EpochGuiRoundedRectTests)
    endif()

    add_executable(EpochGuiInputTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/input_tests.cpp"
    )
    target_link_libraries(EpochGuiInputTests PRIVATE EpochGui)
    target_compile_features(EpochGuiInputTests PRIVATE cxx_std_23)
    set_target_properties(EpochGuiInputTests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    add_test(NAME EpochGui.Input COMMAND EpochGuiInputTests)
endif()
