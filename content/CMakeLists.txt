# JSON content loader. Parses data/*.json into sim::Content and validates it.
# This is the only place a JSON library or the filesystem touches game data.
find_package(nlohmann_json CONFIG REQUIRED)

add_library(content STATIC src/loader.cpp)
add_library(dc::content ALIAS content)
target_include_directories(content PUBLIC include)
target_link_libraries(content PUBLIC dc::sim PRIVATE nlohmann_json::nlohmann_json dc_warnings)
