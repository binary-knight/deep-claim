# The playable client: SDL3 for the window, input and OpenGL context, RmlUi
# for the interface. It links the sim through game_session and never contains
# game rules. Configure with the debug-game or release-game preset, which
# enables the vcpkg "game" feature.
find_package(SDL3 CONFIG REQUIRED)
find_package(RmlUi CONFIG REQUIRED)

# RmlUi's reference SDL + OpenGL 3 backend, vendored from the RmlUi 6.3
# source tree (MIT, see backends/LICENSE-RmlUi.txt). It is sample code, not
# part of the installed library, so it is compiled here with the client's
# warnings relaxed: it is third-party code we do not edit beyond the
# SDL_image removal noted in the file.
add_library(rmlui_backend STATIC
    backends/RmlUi_Backend_SDL_GL3.cpp
    backends/RmlUi_Platform_SDL.cpp
    backends/RmlUi_Renderer_GL3.cpp)
target_include_directories(rmlui_backend PUBLIC backends)
target_link_libraries(rmlui_backend PUBLIC RmlUi::RmlUi SDL3::SDL3)
target_compile_definitions(rmlui_backend PUBLIC RMLUI_SDL_VERSION_MAJOR=3)
if(NOT MSVC)
    target_compile_options(rmlui_backend PRIVATE -w)
else()
    target_compile_options(rmlui_backend PRIVATE /w)
endif()

add_executable(deep_claim
    src/main.cpp
    src/app.cpp
    src/chart_element.cpp
    src/screenshot.cpp
    src/ui_model.cpp)
target_include_directories(deep_claim PRIVATE src)
target_link_libraries(deep_claim PRIVATE dc::game_session dc::content dc::platform rmlui_backend
                                         RmlUi::RmlUi RmlUi::Debugger SDL3::SDL3 dc_warnings)
# Development fallback: the repository's data and assets, so the client runs
# from the build tree. Shipped builds find both next to the executable.
target_compile_definitions(deep_claim PRIVATE DC_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
                                              DC_ASSETS_DIR="${CMAKE_SOURCE_DIR}/game/assets"
                                              DC_PLAYTEST_DIR="${CMAKE_SOURCE_DIR}/playtest")
# DC_BUILD_ID comes from a header regenerated on every build (see the root
# CMakeLists.txt), not from a compile definition: a definition is fixed at
# configure time, and changing it would recompile every source of the target
# rather than main.cpp alone.
target_include_directories(deep_claim PRIVATE "${DC_BUILD_ID_INCLUDE_DIR}")
add_dependencies(deep_claim dc_build_id)

# Copy data and assets next to the binary so a zipped build directory runs.
# A separate target that always runs, so editing an .rml or .json and
# rebuilding refreshes the copy even when no C++ changed.
add_custom_target(deep_claim_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data"
            "$<TARGET_FILE_DIR:deep_claim>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/game/assets"
            "$<TARGET_FILE_DIR:deep_claim>/assets"
    COMMENT "Copying data and assets next to deep_claim"
    VERBATIM)
add_dependencies(deep_claim_assets deep_claim)

# Headless layout probe for diagnosing interface layout without a display.
add_executable(deep_claim_layout_probe src/layout_probe.cpp src/app.cpp src/chart_element.cpp
                                        src/ui_model.cpp)
target_include_directories(deep_claim_layout_probe PRIVATE src)
target_link_libraries(deep_claim_layout_probe PRIVATE dc::game_session dc::content dc::platform
                                                      RmlUi::RmlUi RmlUi::Debugger dc_warnings)
target_compile_definitions(deep_claim_layout_probe PRIVATE DC_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
                                                           DC_ASSETS_DIR="${CMAKE_SOURCE_DIR}/game/assets")
add_test(NAME layout_probe COMMAND deep_claim_layout_probe)
