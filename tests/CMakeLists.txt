# LUDO Map Editor — regression suite
#
# These tests intentionally exercise small policy/header units without Qt so
# architectural regressions are caught quickly. Runtime/bridge tests execute
# the authoritative JavaScript plugins directly from integrations/ when Node
# is available.

function(ludo_add_cpp_regression target source test_name)
    add_executable(${target} "${source}")
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}")
    target_compile_features(${target} PRIVATE cxx_std_17)
    add_test(NAME "${test_name}" COMMAND $<TARGET_FILE:${target}>)
    set_tests_properties("${test_name}" PROPERTIES
        LABELS "core;regression"
        TIMEOUT 20)
    set_property(TARGET ${target} PROPERTY FOLDER "Tests")
endfunction()

ludo_add_cpp_regression(ludo_test_region_gradient
    region_gradient_test.cpp core.region_gradient)
ludo_add_cpp_regression(ludo_test_region_merge
    region_merge_test.cpp core.region_merge)
ludo_add_cpp_regression(ludo_test_publication_policy
    publication_policy_test.cpp core.publication_policy)
ludo_add_cpp_regression(ludo_test_editor_input_policy
    editor_input_policy_test.cpp core.editor_input_policy)

add_executable(ludo_test_asset_database asset_database_test.cpp)
target_link_libraries(ludo_test_asset_database PRIVATE ludo_map_core)
add_test(NAME core.asset_database_v9 COMMAND $<TARGET_FILE:ludo_test_asset_database>)
set_tests_properties(core.asset_database_v9 PROPERTIES LABELS "core;team;regression" TIMEOUT 30)

set(LUDO_REGRESSION_TEST_TARGETS
    ludo_test_asset_database
    ludo_test_region_gradient
    ludo_test_region_merge
    ludo_test_publication_policy
    ludo_test_editor_input_policy)

find_program(LUDO_NODE_EXECUTABLE NAMES node node.exe)
if(LUDO_NODE_EXECUTABLE)
    add_test(NAME runtime.rpgmaker_bridge_smoke
        COMMAND "${LUDO_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/rpgmaker_bridge_smoke_test.js")
    add_test(NAME runtime.depth_contract_v5
        COMMAND "${LUDO_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/depth_runtime_test.js")

    add_test(NAME runtime.syntax.mz_map
        COMMAND "${LUDO_NODE_EXECUTABLE}" --check "${PROJECT_SOURCE_DIR}/integrations/rpg-maker-mz/LudoMapSystem.js")
    add_test(NAME runtime.syntax.mz_camera
        COMMAND "${LUDO_NODE_EXECUTABLE}" --check "${PROJECT_SOURCE_DIR}/integrations/rpg-maker-mz/LudoCameraSystem.js")
    add_test(NAME runtime.syntax.mz_reflection
        COMMAND "${LUDO_NODE_EXECUTABLE}" --check "${PROJECT_SOURCE_DIR}/integrations/rpg-maker-mz/LudoReflectionSystem.js")
    add_test(NAME runtime.syntax.mv_map
        COMMAND "${LUDO_NODE_EXECUTABLE}" --check "${PROJECT_SOURCE_DIR}/integrations/rpg-maker-mv/LudoMapSystem.js")

    set_tests_properties(
        runtime.rpgmaker_bridge_smoke
        runtime.depth_contract_v5
        runtime.syntax.mz_map
        runtime.syntax.mz_camera
        runtime.syntax.mz_reflection
        runtime.syntax.mv_map
        PROPERTIES LABELS "runtime;regression;rpg-maker" TIMEOUT 30)
else()
    message(WARNING
        "Node.js was not found. C++ regression tests remain enabled, but "
        "RPG Maker JavaScript runtime tests will be skipped.")
endif()


find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME server.team_protocol_v9
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/server/test_server.py")
    set_tests_properties(server.team_protocol_v9 PROPERTIES
        LABELS "server;team;regression"
        TIMEOUT 90
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/server")
    add_test(NAME architecture.protocol9
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/protocol9_architecture_test.py")
    set_tests_properties(architecture.protocol9 PROPERTIES
        LABELS "architecture;team;regression" TIMEOUT 20)
else()
    message(WARNING
        "Python 3 was not found. Team Server regression tests will be skipped.")
endif()

# Regression protection is enabled for normal builds by default. This target
# runs after its native test executables are built; JavaScript tests participate
# automatically when Node is present. Disable only for exceptional packaging or
# toolchain diagnosis with -DLUDO_RUN_REGRESSION_TESTS_ON_BUILD=OFF.
option(LUDO_RUN_REGRESSION_TESTS_ON_BUILD
       "Run the LUDO regression suite during a normal build" ON)
if(LUDO_RUN_REGRESSION_TESTS_ON_BUILD)
    add_custom_target(ludo_regression_tests ALL
        COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure --test-dir "${CMAKE_BINARY_DIR}"
        DEPENDS ${LUDO_REGRESSION_TEST_TARGETS}
        USES_TERMINAL
        COMMENT "Running LUDO regression suite")
endif()
