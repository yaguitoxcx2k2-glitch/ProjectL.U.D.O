add_library(ludo_game_pure STATIC
    GameStateStore.cpp
    RuntimeGpuPolicy.cpp
)
target_include_directories(ludo_game_pure PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(ludo_game_pure PUBLIC cxx_std_17)
target_link_libraries(ludo_game_pure PUBLIC ludo_core_pure)
