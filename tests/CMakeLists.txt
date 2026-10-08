find_package(Catch2 3 CONFIG REQUIRED)

add_executable(sim_tests archive_test.cpp determinism_test.cpp fixed_test.cpp ids_test.cpp
                         money_test.cpp random_test.cpp rng_test.cpp)
target_link_libraries(sim_tests PRIVATE dc::sim Catch2::Catch2WithMain dc_warnings)

include(Catch)
catch_discover_tests(sim_tests)
