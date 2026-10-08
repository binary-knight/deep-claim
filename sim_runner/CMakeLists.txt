add_executable(sim_runner main.cpp)
target_link_libraries(sim_runner PRIVATE dc::sim dc_warnings)

# Smoke test used by CI: five in-game years must run and exit cleanly.
add_test(NAME runner_smoke COMMAND sim_runner --seed 1 --days 1800)
