# UI-independent game session: wraps the sim for a client. Owns time control,
# the player's command queue, the AI competitors, the notice log, text
# formatting, the playtest trace and its replay. No SDL or RmlUi here so it
# can be unit tested.
find_package(nlohmann_json CONFIG REQUIRED)

add_library(game_session STATIC src/session.cpp src/format.cpp src/text.cpp src/trace.cpp
                                src/replay.cpp src/png.cpp)
add_library(dc::game_session ALIAS game_session)
target_include_directories(game_session PUBLIC include)
target_link_libraries(game_session PUBLIC dc::sim PRIVATE nlohmann_json::nlohmann_json
                                                          dc_warnings)

# Headless replay of a playtest trace: rebuilds the player's game from the
# recorded seed and orders and prints their view at any day. Built in every
# preset so it is available without the client's dependencies.
add_executable(deep_claim_replay replay_main.cpp)
target_link_libraries(deep_claim_replay PRIVATE dc::game_session dc::content dc_warnings)
target_compile_definitions(deep_claim_replay PRIVATE DC_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
