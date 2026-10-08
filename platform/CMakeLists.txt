# Platform services seam. The game talks to achievements, stats and the save
# directory through this interface; a Steamworks implementation can replace
# NullServices later without touching game or sim code.
add_library(platform STATIC src/null_services.cpp)
add_library(dc::platform ALIAS platform)
target_include_directories(platform PUBLIC include)
target_link_libraries(platform PRIVATE dc_warnings)
