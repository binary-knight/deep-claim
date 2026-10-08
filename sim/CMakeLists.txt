# Pure simulation library. All game rules and state live here.
# It must never depend on SDL, RmlUi, JSON, the filesystem, or Steam.
add_library(sim STATIC src/content.cpp src/fatal.cpp src/fixed.cpp src/money.cpp src/random.cpp
                       src/rng.cpp src/save.cpp src/simulation.cpp)
add_library(dc::sim ALIAS sim)
target_include_directories(sim PUBLIC include)
target_link_libraries(sim PRIVATE dc_warnings)
