find_package(Catch2 3 CONFIG REQUIRED)

add_executable(sim_tests ai_test.cpp archive_test.cpp content_test.cpp data_files_test.cpp design_tokens_test.cpp
                         determinism_test.cpp events_test.cpp finance_test.cpp fixed_test.cpp
                         ids_test.cpp markets_test.cpp math_test.cpp mines_test.cpp money_test.cpp
                         plot_market_test.cpp random_test.cpp research_test.cpp rng_test.cpp
                         strings_test.cpp
                         sell_policy_test.cpp session_test.cpp surveys_test.cpp trace_test.cpp view_source_test.cpp worldgen_test.cpp)
target_link_libraries(sim_tests PRIVATE dc::sim dc::content dc::game_session Catch2::Catch2WithMain
                                        dc_warnings)
target_compile_definitions(sim_tests PRIVATE DC_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
                                             DC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(Catch)
catch_discover_tests(sim_tests)
