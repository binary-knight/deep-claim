# UI-independent game session: wraps the sim for a client. Owns time control,
# the player's command queue, the AI competitors, the notice log and text
# formatting. No SDL or RmlUi here so it can be unit tested.
add_library(game_session STATIC src/session.cpp src/format.cpp)
add_library(dc::game_session ALIAS game_session)
target_include_directories(game_session PUBLIC include)
target_link_libraries(game_session PUBLIC dc::sim PRIVATE dc_warnings)
