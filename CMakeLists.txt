cmake_minimum_required(VERSION 3.24)
project(LudoMapEditor VERSION 0.5.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Phase 2: the editor no longer links the old game/player runtime.  GuiPrivate
# was required by the former QRhi game runtime and is intentionally gone.
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui Widgets Svg Concurrent Network)

include(cmake/MapEditorSources.cmake)

list(LENGTH LUDO_MAP_AUTHORING_SOURCES _ludo_authoring_count)
list(LENGTH LUDO_MAP_PROJECT_SOURCES _ludo_project_count)
list(LENGTH LUDO_MAP_MIGRATION_SOURCES _ludo_migration_count)
message(STATUS "LUDO Map Editor modules: authoring=${_ludo_authoring_count}, project=${_ludo_project_count}, migration=${_ludo_migration_count}")

# ---------------------------------------------------------------------------
# Modules of the desktop map editor.
# ---------------------------------------------------------------------------
add_library(ludo_map_core STATIC ${LUDO_MAP_CORE_SOURCES})
target_include_directories(ludo_map_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ludo_map_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Concurrent Qt6::Network)
target_compile_definitions(ludo_map_core PUBLIC LUDO_MAP_EDITOR_ONLY=1)

add_library(ludo_map_ui STATIC
    ${LUDO_MAP_UI_SOURCES}
    ${LUDO_MAP_MODULE_SOURCES}
    ${LUDO_MAP_PLATFORM_SOURCES})
target_include_directories(ludo_map_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ludo_map_ui PUBLIC
    ludo_map_core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Concurrent Qt6::Network)
target_compile_definitions(ludo_map_ui PUBLIC LUDO_MAP_EDITOR_ONLY=1)

# Generate CollaborationClient's meta-object explicitly. Some Windows kit
# builds reuse pre-generated AUTOMOC output and miss newly introduced classes.
# Exclude these inputs from AUTOMOC so a clean build cannot generate it twice.
set_source_files_properties(
    ui/CollaborationClient.h ui/CollaborationClient.cpp
    PROPERTIES SKIP_AUTOMOC ON)
qt6_wrap_cpp(LUDO_COLLABORATION_MOC
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/CollaborationClient.h"
    TARGET ludo_map_ui)
set_source_files_properties(${LUDO_COLLABORATION_MOC} PROPERTIES SKIP_AUTOMOC ON)
target_sources(ludo_map_ui PRIVATE ${LUDO_COLLABORATION_MOC})


if(WIN32)
    target_compile_definitions(ludo_map_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(ludo_map_ui PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

file(GLOB_RECURSE LUDO_RESOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.qrc")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc")
    list(APPEND LUDO_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc")
endif()
if(NOT LUDO_RESOURCES)
    message(WARNING "Original Qt resources are absent: icons/style/shaders. This is a source-only build and not a complete distribution.")
endif()

# Single desktop product: there is no LudoPlayer target and no gameplay runtime.
qt_add_executable(LudoMapEditor WIN32 main.cpp ${LUDO_RESOURCES})
target_link_libraries(LudoMapEditor PRIVATE ludo_map_ui)
set_target_properties(LudoMapEditor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------------------
# RPG Maker integrations — single source of truth.
#
# Source plugins live ONLY under integrations/. The build copies those exact
# files beside the executable. Historical convenience copies were removed so
# MV/MZ cannot drift from this single authority.
# ---------------------------------------------------------------------------
set(LUDO_RPG_MAKER_INTEGRATIONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/integrations")
set(LUDO_MZ_MAP_PLUGIN "${LUDO_RPG_MAKER_INTEGRATIONS_DIR}/rpg-maker-mz/LudoMapSystem.js")
set(LUDO_MZ_CAMERA_PLUGIN "${LUDO_RPG_MAKER_INTEGRATIONS_DIR}/rpg-maker-mz/LudoCameraSystem.js")
set(LUDO_MZ_REFLECTION_PLUGIN "${LUDO_RPG_MAKER_INTEGRATIONS_DIR}/rpg-maker-mz/LudoReflectionSystem.js")
set(LUDO_MV_MAP_PLUGIN "${LUDO_RPG_MAKER_INTEGRATIONS_DIR}/rpg-maker-mv/LudoMapSystem.js")

foreach(_plugin IN ITEMS "${LUDO_MZ_MAP_PLUGIN}" "${LUDO_MZ_CAMERA_PLUGIN}" "${LUDO_MZ_REFLECTION_PLUGIN}" "${LUDO_MV_MAP_PLUGIN}")
    if(NOT EXISTS "${_plugin}")
        message(FATAL_ERROR "Required RPG Maker integration source is missing: ${_plugin}")
    endif()
endforeach()

# The MV/MZ bridges ship beside the editor so the safe exporter can install
# the engine-specific plugin selected by each project.
add_custom_command(TARGET LudoMapEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:LudoMapEditor>/integrations/rpg-maker-mz"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:LudoMapEditor>/integrations/rpg-maker-mv"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LUDO_MZ_MAP_PLUGIN}"
            "$<TARGET_FILE_DIR:LudoMapEditor>/integrations/rpg-maker-mz/LudoMapSystem.js"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LUDO_MZ_CAMERA_PLUGIN}"
            "$<TARGET_FILE_DIR:LudoMapEditor>/integrations/rpg-maker-mz/LudoCameraSystem.js"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LUDO_MZ_REFLECTION_PLUGIN}"
            "$<TARGET_FILE_DIR:LudoMapEditor>/integrations/rpg-maker-mz/LudoReflectionSystem.js"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LUDO_MV_MAP_PLUGIN}"
            "$<TARGET_FILE_DIR:LudoMapEditor>/integrations/rpg-maker-mv/LudoMapSystem.js"
    VERBATIM)

# ---------------------------------------------------------------------------
# Team Server — ship the cloud/local server tools beside the editor build.
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/server/ludo_server.py")
    add_custom_command(TARGET LudoMapEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:LudoMapEditor>/server"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/server"
                "$<TARGET_FILE_DIR:LudoMapEditor>/server"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Regression protection — CTest + native/JavaScript contract tests.
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
