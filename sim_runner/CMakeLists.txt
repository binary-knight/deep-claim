add_executable(sim_runner main.cpp)
target_link_libraries(sim_runner PRIVATE dc::sim dc::content dc_warnings)
# Default content location; --data overrides it. The shipped game will load
# content from next to its executable instead.
target_compile_definitions(sim_runner PRIVATE DC_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

# Smoke test used by CI: five in-game years must run and exit cleanly.
add_test(NAME runner_smoke COMMAND sim_runner --seed 1 --years 5 --ai 1 --sanity)

# A single run reports how the game ended (spec 010): one that has not ended
# when the run stops is running, with no founder and the never value for its
# founding day. Judged by its output: CTest ignores the exit code of a test
# that has a PASS_REGULAR_EXPRESSION, and runner_smoke judges that.
add_test(NAME runner_outcome COMMAND sim_runner --seed 1 --years 2 --ai 1 --sanity)
set_tests_properties(runner_outcome PROPERTIES PASS_REGULAR_EXPRESSION
    [==["days": 720,.*"outcome": "running", "founder": -1, "founded_day": 721, "end_day": 720, "never_day": 721,.*"program_stage": [0-9]+, "stage_days": \[[0-9]+, [0-9]+, [0-9]+\].*"all_passed": true]==])

# The balance sweep (specs 005, 008 and 010). The game ends: two companies race
# for the program and the first to finish founds it, or the deadline passes
# (spec 010). A sweep that covers the deadline judges the race (BR-013): how
# many seeds end by a founding, how the wins are shared and how many seeds
# see a bankruptcy. sim_runner exits non-zero when a gated check fails: those
# three, and the checks that still mean something in a game that ends early
# (the opening hand, the first mine, a valid world, no rejected AI orders).
# The checks of specs 005 to 008 that assumed a 20-year open-ended game are
# reported and not gated.
#
# An optimized build without the sanitizers runs that sweep whole and is
# judged by its exit code, so a change to the data or the rules that breaks a
# gate fails CI. Thirty years reach the deadline (data/program.json); a seed
# stops when its game ends, so the sweep took 8 to 9 seconds in a Release
# build on the step-1 data, where every game ends in years 8 to 12 (the
# 20-year open-ended sweep before it took 28 to 62). The timeout leaves room
# for a busy machine and for data under which games run to the deadline.
#
# Any other build is slower (a 20-year seed took 0.8 seconds in Release, 5 in
# Debug and 32 under the sanitizers), so it runs a sweep of one or two seeds
# instead. So few seeds can fail a gate by sampling alone, so that run is
# judged by its output and not by its exit code: CTest ignores the exit code
# of a test that has a PASS_REGULAR_EXPRESSION.
#
# The choice is made from what is known at configure time: CMAKE_BUILD_TYPE,
# which every preset sets, and DC_SANITIZE. A build type that is empty or not
# listed counts as not optimized. The Visual Studio generator ignores
# CMAKE_BUILD_TYPE when it builds, but the presets hand the same configuration
# to the build and to ctest, so the two agree there as well.
set(DC_OPTIMIZED_BUILD_TYPES Release RelWithDebInfo MinSizeRel)
if(CMAKE_BUILD_TYPE IN_LIST DC_OPTIMIZED_BUILD_TYPES AND NOT DC_SANITIZE)
    # The command is the one spec 010 gives (AC-010). --sanity adds nothing to a
    # sweep, which always exits non-zero when a gated check fails.
    add_test(NAME runner_sweep COMMAND sim_runner --sweep 50 --years 30 --ai 1 --sanity)
    # Passing must mean the gated checks were judged, never that they were
    # skipped. A reported check may say it was not evaluated: no seed's game
    # need last long enough to measure it.
    set_tests_properties(runner_sweep PROPERTIES
        TIMEOUT 180
        FAIL_REGULAR_EXPRESSION [["gated": true, "detail": "not evaluated]])
else()
    if(DC_SANITIZE)
        set(DC_SHORT_SWEEP_SEEDS 1)
    else()
        set(DC_SHORT_SWEEP_SEEDS 2)
    endif()
    add_test(NAME runner_sweep
        COMMAND sim_runner --sweep ${DC_SHORT_SWEEP_SEEDS} --years 30 --ai 1)
    # The run must reach its last line, every seed's game must have ended, and
    # every check must be reported. The checks that judge each seed on its own
    # must pass: the seeds here are among the 50, so they cannot fail by
    # sampling. The race checks, which judge a count of seeds, must be
    # evaluated, whatever their result. A sanitizer report fails the test even
    # if the output is complete. The expression is matched against the whole
    # output; a dot matches a line break.
    set_tests_properties(runner_sweep PROPERTIES
        TIMEOUT 300
        FAIL_REGULAR_EXPRESSION "Sanitizer;runtime error:;\"outcome\": \"running\""
        PASS_REGULAR_EXPRESSION
        [==["outcome": "(founded|deadline)", "founder": -?[0-9]+, "founded_day": [0-9]+, "end_day": [0-9]+, "stage_days": \[\[[0-9]+, [0-9]+, [0-9]+\], \[[0-9]+, [0-9]+, [0-9]+\]\], "rejected_commands": [0-9]+.*"seeds_founded": [0-9]+,.*"seeds_deadline": [0-9]+,.*"seeds_running": 0,.*"wins": \[[0-9]+, [0-9]+\],.*"founded_day": .*"founder_stage_day_1": .*"founder_stage_day_3": .*"name": "opening_has_mineable_plots", "passed": true, "gated": true.*"name": "unlisted_land_at_year_8", "passed": [a-z]+, "gated": false.*"name": "median_first_mine_day", "passed": [a-z]+, "gated": true.*"name": "world_valid", "passed": true, "gated": true.*"name": "ai_commands_valid", "passed": true, "gated": true.*"name": "founded_before_deadline", "passed": [a-z]+, "gated": true, "detail": "[0-9]+ of [0-9]+ seeds.*"name": "win_balance", "passed": [a-z]+, "gated": true, "detail": "wins by company.*"name": "bankruptcies", "passed": [a-z]+, "gated": true, "detail": "[0-9]+ of [0-9]+ seeds.*"name": "seeds_with_bankruptcy", "passed": [a-z]+, "gated": false.*"name": "mines_leader_over_trailer", "passed": [a-z]+, "gated": false.*"name": "depletion_by_year_8", "passed": [a-z]+, "gated": false.*"name": "bite_iron", "passed": [a-z]+, "gated": false.*"name": "bite_copper", "passed": [a-z]+, "gated": false.*"name": "bite_nickel", "passed": [a-z]+, "gated": false.*"name": "idled_mines", "passed": [a-z]+, "gated": false.*"name": "cash_y10", "passed": [a-z]+, "gated": false.*"name": "bankruptcy_y1_10", "passed": [a-z]+, "gated": false.*"name": "bankruptcy_y11_20", "passed": [a-z]+, "gated": false.*"all_gated_passed": [a-z]+]==])
endif()

# Specs 008 and 010 on a sweep that stops short. The summary carries the
# measures of bite, each seed's row its idle-and-reopen spells beside its
# longest idle run, and a figure for a year the sweep does not reach is null.
# A game still running when the sweep stops is reported as running, with no
# founder and the never value for its founding day. The race checks are not
# evaluated on a sweep that stops before the deadline, so they cannot fail it,
# and the checks of spec 008 are reported and not gated. --sanity is accepted
# with --sweep and changes nothing. The expression is matched against the
# whole output; a dot matches a line break.
add_test(NAME runner_bite_measures COMMAND sim_runner --sweep 2 --years 3 --ai 1 --sanity)
set_tests_properties(runner_bite_measures PROPERTIES PASS_REGULAR_EXPRESSION
    [==["longest_idle_run_days": [0-9]+, "idle_reopen_spells": [0-9]+, "outcome": "running", "founder": -1, "founded_day": 1081, "end_day": 1080, "stage_days": \[\[[0-9]+, [0-9]+, 1081\], \[[0-9]+, [0-9]+, 1081\]\].*"seeds_founded": 0,.*"seeds_deadline": 0,.*"seeds_running": 2,.*"wins": \[0, 0\],.*"founded_day": null,.*"below_breakeven_permille_iron": {"min".*"below_breakeven_permille_lithium": {"min".*"price_year_min_permille_iron": {"min".*"price_year_max_permille_lithium": {"min".*"mine_days_idled_0": {"min".*"mine_days_idled_1": {"min".*"cash_y10_0": null.*"cash_y10_1": null.*"seeds_with_bankruptcy_y1_10": [0-9]+,.*"seeds_with_bankruptcy_y11_20": null,.*"seeds_with_idled_mine": [0-9]+.*"never_day": 1081,.*"name": "ai_commands_valid", "passed": true, "gated": true.*"name": "founded_before_deadline", "passed": true, "gated": true, "detail": "not evaluated.*"name": "win_balance", "passed": true, "gated": true, "detail": "not evaluated.*"name": "bankruptcies", "passed": true, "gated": true, "detail": "not evaluated.*"name": "seeds_with_bankruptcy", "passed": true, "gated": false.*"name": "mean_prices", "passed": [a-z]+, "gated": false.*"name": "bite_iron", "passed": true, "gated": false, "detail": "not evaluated.*"name": "bite_copper", "passed": true, "gated": false, "detail": "not evaluated.*"name": "bite_nickel", "passed": true, "gated": false, "detail": "not evaluated.*"name": "idled_mines", "passed": true, "gated": false, "detail": "not evaluated.*"name": "cash_y10", "passed": true, "gated": false, "detail": "not evaluated.*"name": "bankruptcy_y1_10", "passed": true, "gated": false, "detail": "not evaluated.*"name": "bankruptcy_y11_20", "passed": true, "gated": false, "detail": "not evaluated.*"all_gated_passed": true]==])
