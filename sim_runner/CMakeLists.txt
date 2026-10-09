add_executable(sim_runner main.cpp)
target_link_libraries(sim_runner PRIVATE dc::sim dc::content dc_warnings)
# Default content location; --data overrides it. The shipped game will load
# content from next to its executable instead.
target_compile_definitions(sim_runner PRIVATE DC_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

# Smoke test used by CI: five in-game years must run and exit cleanly.
add_test(NAME runner_smoke COMMAND sim_runner --seed 1 --years 5 --ai 1 --sanity)

# Spec 005: ten seeds for eight years. Fails when any seed opens with fewer
# than two plots in a region mineable on day one, or runs out of unlisted
# land before year eight. The first-mine day is printed but not gated (it
# depends on the AI's bidding, spec 006).
add_test(NAME runner_sweep COMMAND sim_runner --sweep 10 --years 8 --ai 1)
