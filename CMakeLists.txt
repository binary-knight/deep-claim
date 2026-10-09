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
# to the code that produced it. Captured at configure time; reconfigure
# after committing if it matters.
set(DC_BUILD_ID "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE DC_GIT_REV
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE DC_GIT_RESULT
        ERROR_QUIET)
    if(DC_GIT_RESULT EQUAL 0)
        set(DC_BUILD_ID "${DC_GIT_REV}")
    endif()
endif()

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
