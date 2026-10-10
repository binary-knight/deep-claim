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

# Spec 008: the sweep summary carries the measures of bite, and on a sweep
# shorter than 20 years the bite gates are not evaluated, so they cannot fail
# it. The expression is matched against the whole output; a dot matches a
# line break.
add_test(NAME runner_bite_measures COMMAND sim_runner --sweep 2 --years 3 --ai 1)
set_tests_properties(runner_bite_measures PROPERTIES PASS_REGULAR_EXPRESSION
    [["below_breakeven_permille_iron": {"min".*"below_breakeven_permille_lithium": {"min".*"price_year_min_permille_iron": {"min".*"price_year_max_permille_lithium": {"min".*"mine_days_idled_0": {"min".*"mine_days_idled_1": {"min".*"cash_y10_0": null.*"cash_y10_1": null.*"seeds_with_bankruptcy_y1_10": [0-9]+,.*"seeds_with_bankruptcy_y11_20": null,.*"seeds_with_idled_mine": [0-9]+.*"name": "seeds_with_bankruptcy", "passed": true, "gated": true.*"name": "mean_prices", "passed": [a-z]+, "gated": false.*"name": "bite_iron", "passed": true, "gated": true, "detail": "not evaluated.*"name": "bite_copper", "passed": true, "gated": true, "detail": "not evaluated.*"name": "bite_nickel", "passed": true, "gated": true, "detail": "not evaluated.*"name": "idled_mines", "passed": true, "gated": true, "detail": "not evaluated.*"name": "cash_y10", "passed": true, "gated": true, "detail": "not evaluated.*"name": "bankruptcy_y1_10", "passed": true, "gated": true, "detail": "not evaluated.*"name": "bankruptcy_y11_20", "passed": true, "gated": false, "detail": "not evaluated.*"all_gated_passed": true]])

# Spec 008: on a sweep that covers 20 years the bite gates are evaluated. Only
# their presence is tested here, never their result: the untuned economy fails
# them, and the tuning task is what makes the gated sweep pass.
add_test(NAME runner_bite_gates COMMAND sim_runner --sweep 2 --years 20 --ai 1)
set_tests_properties(runner_bite_gates PROPERTIES PASS_REGULAR_EXPRESSION
    [["cash_y10_0": {"min".*"seeds_with_bankruptcy_y11_20": [0-9]+,.*"name": "seeds_with_bankruptcy", "passed": [a-z]+, "gated": false.*"name": "bite_iron", "passed": [a-z]+, "gated": true, "detail": "iron: .*"name": "bite_copper", "passed": [a-z]+, "gated": true, "detail": "copper: .*"name": "bite_nickel", "passed": [a-z]+, "gated": true, "detail": "nickel: .*"name": "idled_mines", "passed": [a-z]+, "gated": true, "detail": "[0-9]+ of 2 seeds.*"name": "cash_y10", "passed": [a-z]+, "gated": true, "detail": "company 0.*"name": "bankruptcy_y1_10", "passed": [a-z]+, "gated": true, "detail": "[0-9]+ of 2 seeds.*"name": "bankruptcy_y11_20", "passed": [a-z]+, "gated": false, "detail": "[0-9]+ of 2 seeds]])
