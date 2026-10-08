find_package(Catch2 3 CONFIG REQUIRED)

add_executable(sim_tests rng_test.cpp determinism_test.cpp)
target_link_libraries(sim_tests PRIVATE dc::sim Catch2::Catch2WithMain dc_warnings)

include(Catch)
catch_discover_tests(sim_tests)
