add_executable(sim_runner main.cpp)
target_link_libraries(sim_runner PRIVATE dc::sim dc::content dc_warnings)
# Default content location; --data overrides it. The shipped game will load
# content from next to its executable instead.
target_compile_definitions(sim_runner PRIVATE DC_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

# Smoke test used by CI: five in-game years must run and exit cleanly.
add_test(NAME runner_smoke COMMAND sim_runner --seed 1 --years 5 --ai 1 --sanity)

# The balance sweep (specs 005 and 008). Its checks are set for 50 seeds over
# 20 years with one rival, and sim_runner exits non-zero when a gated one
# fails: the land and opening checks of spec 005 and the bite gates of spec
# 008 (design record 003).
#
# An optimized build without the sanitizers runs that sweep whole and is
# judged by its exit code, so a change to the data or the rules that breaks a
# gate fails CI. It took 28 to 62 seconds in a Release build, depending on
# what else the machine was doing; the timeout leaves room for a busy one.
#
# Any other build is too slow for it (a seed of 20 years took 0.8 seconds in
# Release, 5 in Debug and 32 under the sanitizers), so it runs a sweep of one
# or two seeds instead. So few seeds can fail a gate by sampling alone, so
# that run is judged by its output and not by its exit code: CTest ignores
# the exit code of a test that has a PASS_REGULAR_EXPRESSION.
#
# The choice is made from what is known at configure time: CMAKE_BUILD_TYPE,
# which every preset sets, and DC_SANITIZE. A build type that is empty or not
# listed counts as not optimized. The Visual Studio generator ignores
# CMAKE_BUILD_TYPE when it builds, but the presets hand the same configuration
# to the build and to ctest, so the two agree there as well.
set(DC_OPTIMIZED_BUILD_TYPES Release RelWithDebInfo MinSizeRel)
if(CMAKE_BUILD_TYPE IN_LIST DC_OPTIMIZED_BUILD_TYPES AND NOT DC_SANITIZE)
    add_test(NAME runner_sweep COMMAND sim_runner --sweep 50 --years 20 --ai 1)
    # Passing must mean the checks were judged, never that they were skipped.
    set_tests_properties(runner_sweep PROPERTIES
        TIMEOUT 180
        FAIL_REGULAR_EXPRESSION "not evaluated")
else()
    if(DC_SANITIZE)
        set(DC_SHORT_SWEEP_SEEDS 1)
    else()
        set(DC_SHORT_SWEEP_SEEDS 2)
    endif()
    add_test(NAME runner_sweep
        COMMAND sim_runner --sweep ${DC_SHORT_SWEEP_SEEDS} --years 20 --ai 1)
    # The run must reach its last line and report every check. The four checks
    # that judge each seed on its own must pass: the seeds here are among the
    # 50, so they cannot fail by sampling. The checks that judge a median or a
    # count of seeds must be evaluated, whatever their result. A sanitizer
    # report fails the test even if the output is complete. The expression is
    # matched against the whole output; a dot matches a line break.
    set_tests_properties(runner_sweep PROPERTIES
        TIMEOUT 300
        FAIL_REGULAR_EXPRESSION "Sanitizer;runtime error:"
        PASS_REGULAR_EXPRESSION
        [["cash_y10_0": {"min".*"seeds_with_bankruptcy_y11_20": [0-9]+,.*"name": "opening_has_mineable_plots", "passed": true, "gated": true.*"name": "unlisted_land_at_year_8", "passed": true, "gated": true.*"name": "median_first_mine_day", "passed": [a-z]+, "gated": true.*"name": "world_valid", "passed": true, "gated": true.*"name": "seeds_with_bankruptcy", "passed": [a-z]+, "gated": false.*"name": "mines_leader_over_trailer", "passed": [a-z]+, "gated": true.*"name": "depletion_by_year_8", "passed": true, "gated": true.*"name": "bite_iron", "passed": [a-z]+, "gated": true, "detail": "iron: .*"name": "bite_copper", "passed": [a-z]+, "gated": true, "detail": "copper: .*"name": "bite_nickel", "passed": [a-z]+, "gated": true, "detail": "nickel: .*"name": "idled_mines", "passed": [a-z]+, "gated": true, "detail": "[0-9]+ of [0-9]+ seeds.*"name": "cash_y10", "passed": [a-z]+, "gated": true, "detail": "company 0.*"name": "bankruptcy_y1_10", "passed": [a-z]+, "gated": true, "detail": "[0-9]+ of [0-9]+ seeds.*"name": "bankruptcy_y11_20", "passed": [a-z]+, "gated": false, "detail": "[0-9]+ of [0-9]+ seeds.*"all_gated_passed": [a-z]+]])
endif()

# Spec 008: the sweep summary carries the measures of bite, and on a sweep
# shorter than 20 years the bite gates are not evaluated, so they cannot fail
# it. No 20-year sweep takes that path, which is why this three-year one is
# kept beside runner_sweep. The expression is matched against the whole
# output; a dot matches a line break.
add_test(NAME runner_bite_measures COMMAND sim_runner --sweep 2 --years 3 --ai 1)
set_tests_properties(runner_bite_measures PROPERTIES PASS_REGULAR_EXPRESSION
    [["below_breakeven_permille_iron": {"min".*"below_breakeven_permille_lithium": {"min".*"price_year_min_permille_iron": {"min".*"price_year_max_permille_lithium": {"min".*"mine_days_idled_0": {"min".*"mine_days_idled_1": {"min".*"cash_y10_0": null.*"cash_y10_1": null.*"seeds_with_bankruptcy_y1_10": [0-9]+,.*"seeds_with_bankruptcy_y11_20": null,.*"seeds_with_idled_mine": [0-9]+.*"name": "seeds_with_bankruptcy", "passed": true, "gated": true.*"name": "mean_prices", "passed": [a-z]+, "gated": false.*"name": "bite_iron", "passed": true, "gated": true, "detail": "not evaluated.*"name": "bite_copper", "passed": true, "gated": true, "detail": "not evaluated.*"name": "bite_nickel", "passed": true, "gated": true, "detail": "not evaluated.*"name": "idled_mines", "passed": true, "gated": true, "detail": "not evaluated.*"name": "cash_y10", "passed": true, "gated": true, "detail": "not evaluated.*"name": "bankruptcy_y1_10", "passed": true, "gated": true, "detail": "not evaluated.*"name": "bankruptcy_y11_20", "passed": true, "gated": false, "detail": "not evaluated.*"all_gated_passed": true]])
