add_library(ludo_core_pure STATIC
    Value.cpp
    JsonValue.cpp
    FileSystem.cpp
    ImageBuffer.cpp
)
target_include_directories(ludo_core_pure PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_compile_features(ludo_core_pure PUBLIC cxx_std_17)
set_target_properties(ludo_core_pure PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(ludo_core_pure_smoke EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/pure/pure_core_smoke.cpp
)
target_link_libraries(ludo_core_pure_smoke PRIVATE ludo_core_pure)
target_compile_features(ludo_core_pure_smoke PRIVATE cxx_std_17)
set_target_properties(ludo_core_pure_smoke PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
