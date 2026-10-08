find_package(Catch2 3 CONFIG REQUIRED)

add_executable(sim_tests archive_test.cpp content_test.cpp data_files_test.cpp
                         determinism_test.cpp fixed_test.cpp ids_test.cpp math_test.cpp
                         money_test.cpp random_test.cpp rng_test.cpp worldgen_test.cpp)
target_link_libraries(sim_tests PRIVATE dc::sim dc::content Catch2::Catch2WithMain dc_warnings)
target_compile_definitions(sim_tests PRIVATE DC_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

include(Catch)
catch_discover_tests(sim_tests)
