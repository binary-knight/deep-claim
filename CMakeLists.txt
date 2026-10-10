cmake_minimum_required(VERSION 3.25)
project(DeepClaim VERSION 0.1.0 LANGUAGES CXX)

option(DC_BUILD_TESTS "Build unit tests" ON)
option(DC_BUILD_RUNNER "Build the headless sim_runner" ON)
option(DC_BUILD_GAME "Build the SDL3/RmlUi game client" OFF)
option(DC_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(DC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Short git revision, recorded in playtest traces so a trace can be matched
# to the code that produced it. It is read on every build, not at configure
# time, so a build made after a commit names that commit: the dc_build_id
# target runs cmake/BuildId.cmake, which writes generated/dc_build_id.h only
# when the revision has changed. The id is "unknown" outside a git checkout.
# A target that consumes the header depends on dc_build_id and adds
# DC_BUILD_ID_INCLUDE_DIR to its include path (see game/CMakeLists.txt).
set(DC_BUILD_ID_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
set(DC_BUILD_ID_HEADER "${DC_BUILD_ID_INCLUDE_DIR}/dc_build_id.h")
find_package(Git QUIET)
set(DC_BUILD_ID_COMMAND
    "${CMAKE_COMMAND}" "-DDC_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DDC_BUILD_ID_HEADER=${DC_BUILD_ID_HEADER}" "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
    -P "${CMAKE_SOURCE_DIR}/cmake/BuildId.cmake")
# Once at configure time as well, so the header exists for tools that read
# compile_commands.json without building first.
execute_process(COMMAND ${DC_BUILD_ID_COMMAND})
# BYPRODUCTS tells Ninja where the header comes from; a custom target is
# always out of date, so the script runs on every build.
add_custom_target(dc_build_id ALL
    COMMAND ${DC_BUILD_ID_COMMAND}
    BYPRODUCTS "${DC_BUILD_ID_HEADER}"
    COMMENT "Reading the build id"
    VERBATIM)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)

add_subdirectory(sim)
add_subdirectory(content)
add_subdirectory(platform)
add_subdirectory(game/session)

# enable_testing() must precede any add_test(), so the runner's smoke test
# registers with CTest; the runner is added below for that reason.
if(DC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(DC_BUILD_RUNNER)
    add_subdirectory(sim_runner)
endif()

if(DC_BUILD_GAME)
    add_subdirectory(game)
endif()
