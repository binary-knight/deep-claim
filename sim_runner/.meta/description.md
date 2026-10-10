# sim_runner

**Purpose:** Headless balance-run executable (`sim_runner`). It builds a `sim::Simulation` from the JSON content in `data/`, drives every company (the player's slot included) with `sim::HeuristicController` for a number of in-game days, and prints a JSON report on stdout, optionally a CSV file, with an exit code that encodes whether the economy checks passed. It has two modes: a single-seed run (optionally gated by `--sanity`) and a multi-seed `--sweep N` that measures the spec 005 (world and opening) and spec 006 (balance) goals.

## Key Components
- `CMakeLists.txt` -- defines the `sim_runner` executable from `main.cpp`, links `dc::sim`, `dc::content`, `dc_warnings`, sets the compile definition `DC_DATA_DIR="${CMAKE_SOURCE_DIR}/data"`, and registers two CTest tests: `runner_smoke` (`sim_runner --seed 1 --years 5 --ai 1 --sanity`) and `runner_sweep` (`sim_runner --sweep 10 --years 8 --ai 1`).
- `main.cpp` -- the whole program (1100 lines, everything except `main` in an anonymous namespace). Parts, in file order:
  - Argument parsing: `Options`, `parseUnsigned`, `parseNumericOption`, `parseArgs`. Flags: `--seed N`, `--days N`, `--years N`, `--ai N`, `--data DIR`, `--csv FILE`, `--sanity`, `--sweep N`.
  - Single run: `run` (per day: `Simulation::collectCommands` for each company, then `Simulation::tick`), `recordDay` (accumulates `RunStats`: per-market min/max/sum price and days at floor/cap, rejected-command counts by `sim::rejectReasonName`, bankruptcy day, first-mine day, peak cash, peak debt, yearly `sim::validateWorld` failures), `writeCsvHeader`/`writeCsvRows` (one row per company at each month end), `buildChecks` (five checks: `world_valid`, `no_runaway_money`, `prices_sane`, `ai_commands_valid`, `economy_alive`), `printSummary` (JSON).
  - Sweep: `SweepRow` (per-seed figures), `sweepSeed` (plays one seed), `describeQueue` (opening hand classified with `sim::classifyRegionAtStart`, longest same-region run in the listing queue), `recordFirstMine`, `recordLand`, `recordContest`, `finishContest`, `sweepChecks` (spec 005 goals), `contestChecks` (spec 006 goals), `printSweep`/`printSweepRows`/`printContestSummary`/`contestJson` (JSON), `writeSweepCsv`/`writeContestCsv` (one CSV row per seed), `runSweep`.
  - Helpers: `saturatingAdd`, `jsonString`, `Spread`/`spreadOf`/`spreadOver` (min, lower median, max), `dayOrNever`, `minesRatioPercent`, `pooledMineLives`.
  - `main` -- parses args, loads content with `content::loadFromDirectory`, dispatches to `runSweep` or the single run.

## Dependencies
- `sim/` (CMake target `dc::sim`): headers `sim/ai_controller.h`, `sim/commands.h`, `sim/content.h`, `sim/fixed.h`, `sim/ids.h`, `sim/money.h`, `sim/save.h`, `sim/simulation.h`, `sim/types.h`, `sim/view.h`, `sim/world_state.h`, `sim/worldgen.h`. Symbols used include `sim::Simulation` (`collectCommands`, `tick`, `state`, `view`, `stateHash`), `sim::HeuristicController`, `sim::GameSetup`, `sim::validateWorld`, `sim::classifyRegionAtStart`, `sim::checkedMulDiv`, `sim::Money`, `sim::toString`, `sim::TickReport`/`sim::Notice`/`sim::NoticeKind`, `sim::MineState`, `sim::kDaysPerYear`, `sim::kDaysPerMonth`, `sim::kMaxDay`.
- `content/` (CMake target `dc::content`): `content/loader.h` for `content::loadFromDirectory`, `content::LoadResult`, `content::formatErrors`.
- `cmake/Warnings.cmake` (interface target `dc_warnings`) for compiler warning flags.
- `data/` at runtime: the default content directory, baked in through `DC_DATA_DIR`; `--data DIR` overrides it.
- C++20 standard library only otherwise (`<format>`, `<span>`, `<fstream>`, `<map>`, ranges algorithms). No direct third-party include; nlohmann-json is reached only through `dc::content`.
- Built only when the root `CMakeLists.txt` option `DC_BUILD_RUNNER` is ON (the default). Consumers: CTest (`runner_smoke`, `runner_sweep`), CI through `ctest --preset` in `.github/workflows/ci.yml`, and the manual balance runs described in `README.md` and `docs/process.md`.

## Patterns
- Single translation unit, free functions in an anonymous namespace, no classes with behavior; plain structs (`Options`, `RunStats`, `MarketStats`, `Check`, `SweepRow`, `Spread`) carry state between the phases parse -> simulate -> record -> check -> print.
- All companies are AI-driven with one shared `sim::HeuristicController`; company 0 is the player's slot, named "Player (AI)", rivals are "Rival 1..N" (`--ai N`).
- JSON is hand-written with `std::format` to stdout (no JSON library); CSV goes to the `--csv` file. Money is printed with `sim::toString`.
- Single-run JSON top-level keys: `seed`, `days`, `state_hash`, `companies`, `markets`, `rejections`, `checks`, `all_passed`. Single-run CSV columns: `day,company,cash,debt,plots,mines_operating,mines_mothballed,mines_depleted,inventory_value` then one `price_<resource id>` per resource.
- Sweep JSON top-level keys: `sweep` (`first_seed`, `seeds`, `days`, `ai`), `rows` (one object per seed), `sample_years` ([2, 5, 8]), `summary` (min/median/max per figure), `never_day`, `checks` (each with `gated`), `all_gated_passed`. Sweep CSV: one row per seed with opening, listing, unlisted/withdrawn-by-year, first-mine, `world_valid`, per-company `final_cash_<c>`, `mines_built_<c>`, `mines_operating_<c>`, `bankrupt_day_<c>`, then `first_depletion_day`, `depleted_by_y8`, and `price_mean_permille_<r>` per resource index.
- Checks are a `std::vector<Check>` of name/passed/detail/gated. In a sweep a check with `gated == false` is printed but does not affect the exit code. Gated sweep checks: `opening_has_mineable_plots`, `unlisted_land_at_year_8`, `median_first_mine_day`, `world_valid`, `seeds_with_bankruptcy`, `mines_leader_over_trailer`, `depletion_by_year_8`. Reported only: `mean_prices`, `median_mine_life`, `withdrawn_land_at_year_8`.
- Goal thresholds are `constexpr` constants at the top of the sweep section (`kGoalMineableOpening`, `kGoalLandYear`, `kGoalFirstMineDay`, `kGateMinesRatioPercent`, `kGoalPriceMeanPermille`, `kGoalMineLifeMinDays`/`MaxDays`, `kGoalWithdrawnYear8`, ...), citing `docs/specs/005-world-and-opening.md` and `docs/specs/006-balance.md`.
- Integer arithmetic throughout the measurements: cents, permille and percent in `int64_t`, sums through `saturatingAdd`, scaling through `sim::checkedMulDiv`; doubles appear only when formatting ratios for output.
- Unmeasured figures are emitted as JSON `null` (blank CSV cell), never zero; a day that never happened is 0 per row and `days + 1` (`never_day`) in spreads; medians are lower medians so the value is one that occurred.

## Constraints
- Exit codes: 0 success; 1 content failed to load, the CSV could not be opened or written, or stdout failed; 2 usage error; 3 checks failed (single run: only when `--sanity` is given; sweep: any gated check failed).
- Argument rules: every flag except `--sanity` takes a value; numeric values must be decimal digits only. `--days` 1..`sim::kMaxDay`; `--years` 1..10000 (sets days = years * `sim::kDaysPerYear`; the later of `--days`/`--years` wins); `--ai` 0..16; `--sweep` 1..10000; `--seed` any unsigned 64-bit value. `--sweep` combined with `--sanity` is a usage error, as is a sweep whose seed range `seed..seed+N-1` would wrap. Defaults: seed 1, 20 years, 1 AI rival, no CSV, no sanity, no sweep.
- Without `--sanity` a single run still computes and prints the checks and `all_passed`, but exits 0.
- A check the run is too short to measure passes as "not evaluated" (`economy_alive` needs 2 years; land, depletion and withdrawn goals need 8 years; first-mine needs 250 days; mine life needs more than 7 years).
- First-mine figures in a sweep refer to company 0 only. The mine-life cohort is mines that opened at least `kMineLifeCohortDays` (7 years) before the end; mines closed by bankruptcy before depleting are excluded.
- `jsonString` escapes only `"` and `\`; control characters in names or ids are not escaped.
- In sweep mode the CSV stream is not checked on open, only after the write; the JSON is printed before the CSV is written.
- The comment on `runner_sweep` in `CMakeLists.txt` says the first-mine day is printed but not gated; in `main.cpp` the `median_first_mine_day` check is gated (the comment above `kGoalMineableOpening` says spec 006 made it so).
- `DC_DATA_DIR` is an absolute source-tree path fixed at configure time, so a relocated binary needs `--data`.
- No unit tests of its own; it is exercised only through the two CTest command lines.
