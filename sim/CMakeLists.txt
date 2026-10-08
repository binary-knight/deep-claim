# Pure simulation library. All game rules and state live here.
# It must never depend on SDL, RmlUi, JSON, the filesystem, or Steam.
add_library(sim STATIC src/commands.cpp src/content.cpp src/fatal.cpp src/finance.cpp
                       src/fixed.cpp src/markets.cpp src/math.cpp src/mines.cpp src/money.cpp
                       src/plot_market.cpp src/random.cpp src/rng.cpp src/save.cpp
                       src/simulation.cpp src/surveys.cpp src/tech.cpp src/view.cpp
                       src/worldgen.cpp)
add_library(dc::sim ALIAS sim)
target_include_directories(sim PUBLIC include)
target_link_libraries(sim PRIVATE dc_warnings)
