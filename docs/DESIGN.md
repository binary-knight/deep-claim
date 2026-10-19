# Deep Claim design decisions

Short records of decisions and why. Newest at the bottom.

## Architecture split

`sim/` is a pure C++20 static library with every rule and all state, and no
third-party dependencies. `content/` (step 3) parses JSON into the sim's plain
content structs so the sim never sees a JSON library or a file. `sim_runner/`
is a headless CLI for balance testing. `game/` (step 15) is SDL3 + RmlUi and
only forwards player commands. `platform/` will hold a Steam-shaped interface
with a null implementation.

## Dependency manager: vcpkg manifest mode

vcpkg is a git submodule at `external/vcpkg`, pinned by `builtin-baseline`.
Reasons: the RmlUi port wires up FreeType, binary caching (vcpkg `files` backend
persisted with actions/cache) keeps Windows CI fast, and the sim-only side needs only Catch2.
Game-client dependencies live behind the `game` manifest feature so CI does
not build SDL3 and RmlUi until the client exists.

## Determinism rules

- 1 tick = 1 in-game day. Calendar is 12 months of 30 days (360-day year), so
  monthly costs fall on fixed day boundaries and tests stay simple.
- Own RNG (PCG32) with one stream per system. Standard library distributions
  differ between libstdc++ and MSVC.
- Money is int64 cents, quantities are integer tonnes, ratios are fixed-point.
  No floating point in sim state, so state hashes match across compilers.
- State hash is FNV-1a over a canonical little-endian byte stream, never
  `std::hash`. A golden hash recorded on Linux/GCC is checked on Windows/MSVC
  in CI.
- No iteration over `std::unordered_map` during a tick.
- Tick pipeline, fixed order: apply commands (sorted by company id, then
  submission order) -> program: the deadline check (1b, spec 010) ->
  resolve auctions -> advance surveys -> mines extract -> sell policies (4b,
  spec 001) -> update markets -> month-boundary finance and bankruptcy ->
  research -> events -> advance day -> the pace check (9b, spec 011) ->
  emit tick report. The game can end in the commands step or the deadline
  check; the world then freezes: no later step runs and the day does not
  advance. See "The race for the program (spec 010, step 1)". The pace
  check is the one step after the day advances; see "Reading the race
  (spec 011)".

## Hidden truth vs observable view

True deposits live only in `WorldState::hidden`. A view function builds a
per-company view (public plot data, that company's surveys, market history,
own finances) and never reads `hidden`. AI controllers receive only the view.

## Sanitizers

The asan preset enables ASan and UBSan and builds the sim stack only, because
SDL and font libraries produce leak noise. MSVC has no UBSan, so the sanitizer
CI leg runs on Ubuntu only; Windows CI builds debug and release.

## Tooling

Catch2 v3 for tests. clang-format (Google base, 4-space indent, 100 columns)
and clang-tidy are enforced in CI. Warnings are errors by default
(`DC_WARNINGS_AS_ERRORS`).

## Core types (step 2)

- `Fixed` is decimal fixed-point with six fractional digits (raw = value x
  1,000,000). Decimal rather than binary so JSON values like 0.7 convert
  exactly and debug output is readable.
- All Fixed and Money multiplies and divides truncate toward zero and funnel
  through one `mulDiv(a, b, c)` with a 128-bit intermediate (`__int128` on
  GCC/Clang, `_umul128`/`_udiv128` on MSVC). `checkedMulDiv` reports overflow;
  `mulDiv` asserts on it. Tests pin literal results so the MSVC path is
  verified on every CI run.
- `Fixed::fromDouble` rounds to nearest and exists only for the content
  loader boundary. Sim logic never touches floating point.
- `StrongId<Tag>` is a uint32 index into the owning vector. Entities are never
  erased from WorldState vectors; dead ones carry a flag. This keeps ids stable
  for saves, replays and UI references.
- `RngStreams` holds one PCG32 per system (world, market, survey, event, ai)
  derived from the seed with distinct stream ids.
- `approxNormal` is the Irwin-Hall sum of twelve uniforms: integer-only,
  bounded to six sigma, good enough for noise models.
- Serialization is one `serialize(archive, value)` per type driving three
  archives: hash (FNV-1a), write and read. The read path never throws; it sets
  a failure flag and the caller checks it once. Length prefixes are checked
  against remaining bytes before allocation. `hashOf(x)` equals FNV-1a over
  `toBytes(x)` by construction.
- Save files are the magic "DCSV", a uint32 format version, then the
  WorldState. Until the first public build, `kSaveVersion` stays at 1 and
  saves are simply not compatible across commits; versioning and migration
  start when players have saves worth keeping.
- All Fixed and Money arithmetic is checked: add, subtract and negate go
  through `checkedAdd`/`checkedSub`/`checkedNegate`, and every multiply or
  divide (including by plain integers) goes through `mulDiv`. There is no
  unchecked signed arithmetic on sim quantities.
- `mulDiv` aborts in every build type on overflow or division by zero. A
  crash with a message is easier to find than a silently corrupted balance,
  and overflow indicates a design bug, not a runtime condition.
- The archive's vector path does not support `std::vector<bool>`. Entity
  flags use sized integer or enum types.

## Content and data files (step 3)

- `sim::Content` holds plain structs (ResourceDef, TechDef, RegionDef,
  EventDef, BalanceParams). The `content` library owns nlohmann-json and the
  filesystem; the sim never sees either. Tests build content in code or from
  JSON strings without touching disk.
- Numbers in JSON are human units: money in whole currency units with up to
  two decimals, ratios as decimals with up to six. The loader parses doubles,
  rejects non-finite values, magnitudes above 1e11 (money) or 1e6 (ratios),
  and excess precision down to a thousandth of a unit, then rounds to
  nearest. Finer excess digits are rounded silently; that is the accepted
  limit of parsing via double (decision: double-then-round over digit-string
  parsing). Duplicate JSON keys are not detected (last one wins), a known gap
  of DOM parsing.
  Truncation is the rule inside the sim, never at the content boundary.
- Validation collects every error (file, JSON path, message) and fails the
  whole load. Unknown keys are errors so typos cannot silently become
  defaults. Cross-file references are checked both ways: a resource's
  `unlocked_by` tech must carry the matching `unlock_resource` effect.
- Tests and sim_runner locate `data/` through a compile definition set by
  CMake; the runner's `--data` flag overrides it. The game will later ship
  data next to its executable.
- Mine output is per resource (`base_output_tonnes_per_day`) rather than a
  single global number, so that a lithium mine moves tonnes at a scale that
  matches lithium demand. Demand baselines are set at roughly one and a half
  to two mines' output.
- Saves will carry a fingerprint of the loaded content once WorldState holds
  content ids (step 5), so a save cannot be opened against reordered data.
- Deposit priors are log-normal, written as `tonnes_median` and
  `tonnes_spread` (ratio between the 84th percentile and the median) because
  those are easier to tune by hand than log-space parameters. World
  generation (step 4) needs a fixed-point power function for this.
- Ids in data files are lowercase snake_case so they can double as stable
  save/replay identifiers.

### Balance numbers, first pass (all provisional until sim_runner sweeps)

- Starting capital 5.0M against plots at 0.4M to 4M and mines at 4000 per
  hectare (0.8M to 8M): the player can afford roughly one plot and one mine
  before needing a loan or revenue. That is the intended early tension.
- Loan at 8 percent over 60 months, capped at 10M, so leverage is available
  but a bad first mine can bankrupt.
- Survey at 150k and 20 days (auctions run 45 days so a survey commissioned at
  listing informs the bid) with base relative noise 0.5; the three survey
  techs bring it down to 0.5 x 0.8 x 0.7 x 0.6 = 0.17.
- Mine opex is 12k/day for every mine. Net margins per mine at base prices:
  iron 400 t x 60 = 24k gross, 12k net; copper 60 t x 900 = 54k gross, 42k
  net; nickel 20 t x 2200 = 44k gross, 32k net; lithium 5 t x 14000 = 70k
  gross, 58k net. An iron mine on a 4M plot pays back in about a year;
  richer resources pay back faster but are rarer and, for lithium, gated
  behind a 240-day tech chain.
- Demand baselines: iron 800 t/day (2 mines), copper 100 (1.7 mines), nickel
  30 (1.5 mines), lithium 8 (1.6 mines). A second mine on the same resource
  starts to depress the price; a third does so visibly.
- Event daily weights sum to 0.0021 once all are eligible, so about one event
  starts every 480 days.

## World generation (step 4)

- `Simulation` takes a `const Content&` that must outlive it; the world is
  generated in the constructor from the `world` RNG stream. Nothing in
  WorldState points at content by anything but ids.
- Plots are generated region by region in content order. Area is uniform in
  the region's range. Ask price interpolates linearly between the region's
  min and max by area and then takes +/- 20% noise, so price hints at size
  but never at deposits.
- Each deposit prior is rolled independently: present with
  `presence_prob`, size `median * spread^z` with z ~ approx-normal clamped to
  +/- 3 sigma, scaled by plot area relative to the region's mean area, and
  bounded to [1, 1e15] tonnes. `spread^z` uses integer-only `fixedExp` and
  `fixedLn` (sim/math.h), so generation is bit-identical across platforms.
- Negative z is applied by dividing by the positive power, because a factor
  below one in six decimals loses relative precision at the lower tail.
- Hidden truth lives in `WorldState::hidden`, a separate struct, so the view
  code added in step 5 can be audited for never reading it.
- Loading a save runs `validateWorld`: vector sizes agree, ids are in range
  for the content, tonnages are consistent, enums are known. A save that
  passes the fingerprint check but fails validation is rejected, so later
  systems can index by id without bounds checks.
- Archive reads bound each container by its real minimum serialized size
  (measured once from a default value) and turn allocation failure into the
  error flag. A variant is the exception: its default value is its first
  alternative, which need not be its smallest, so a variant's minimum is
  its smallest alternative plus the tag (see "Pending orders (spec 007)").
- Saves carry `hashOf(content)` in the header and refuse to load against
  different content. Reordering resources in data files would otherwise
  silently retarget every ResourceId in a save.
- The golden determinism hash uses an in-code content fixture
  (tests/test_content.h), so tuning data files does not move it. A separate
  test checks that the shipped data generates a sane world.

## Companies, commands and the plot market (steps 5 and 6)

- Company 0 is always the player; AI companies follow in `GameSetup` order.
  All start with `starting_capital` and an empty inventory.
- Commands are a `std::variant` queued with `Simulation::submit` and applied
  at the start of the next tick, stable-sorted by company id so submission
  order only matters within one company. A rejected command produces a
  `CommandRejected` notice with a reason; it never throws or aborts. The
  replay of a game is the seed, the setup and the command log.
- `TickReport` is transient (not state) and lists what happened in the tick
  for UIs and logs: listings, relistings, wins, losses, rejections.
- Plots enter the market from a listing queue shuffled at generation:
  `plots_listed_at_start` on day 0, then `plots_listed_per_month` on every
  month boundary. Each listing opens a sealed-bid auction closing after
  `auction_window_days`.
- Bids: at least the ask, at most the company's uncommitted cash. Cash is
  reserved by standing bids on open auctions so a company cannot promise
  the same money twice; a rebid on the same plot replaces the old bid.
  Highest bid wins, ties go to the earlier bid, the winner pays its bid and
  the plot becomes Sold. Bidders who can no longer pay at close are skipped.
- Unsold auctions relist immediately at 90% of the ask, never below half the
  generated land value, so ignored plots get cheaper over time.
- `CompanyView` is the only thing controllers and the UI read. It carries
  public plot data, the viewer's own bid and the bid count on each open
  auction (amounts stay sealed), own cash and reservations, and public facts
  about competitors. `view.cpp` must never mention hidden state; a test
  greps the source for it.
- Notices are public (listings, relistings, sales with the price paid, as a
  land registry would publish) or private (rejections, losses, forfeits).
  `Simulation::reportFor(company)` filters; the unfiltered report exists for
  tests and tooling. Notices carry the auction id so a UI can refer to the
  right auction after an immediate relisting.
- `availableCash` (cash minus reservations) is the single affordability
  check every future spending command validates against.
- Hosts that run controllers call `collectCommands`, which binds each
  command to the controller's company; a controller cannot act for anyone
  else.
- Saves include the commands queued for the next tick, so saving between
  submitting and ticking loses nothing.
- `validateWorld` also checks company, auction and listing-queue invariants:
  exactly one open auction per listed plot, consumed queue entries are
  listed or sold and the rest unlisted, one bid per company per auction,
  unique order stamps below the counter, representable reservation totals,
  and days within `kMaxDay` (10,000 years). Systems can then index by id
  without bounds checks.

## Surveys and mines (steps 7 and 8)

- Any plot not owned by another company may be surveyed, including listed
  plots, so bidding can be informed. Cost is paid at commission; results
  arrive after `survey_duration_days`. One pending survey per company per
  plot; repeat surveys after completion are allowed and independent, so a
  player may pay to average readings.
- A reading is `remaining tonnes * exp(sigma * z)` per deposit actually
  present (z clamped to 3 sigma, from the survey RNG stream), with sigma =
  `survey_base_noise` times the product of completed survey techs. The
  reading carries sigma so the UI can show an error band. Absent resources
  are not reported; presence is what surveys get right.
- Surveys belong to the commissioning company and appear only in its view.
- Tech effects are read through `techMultiplier` and `resourceUnlocked`
  (sim/tech.h) from `Company::completedTechs`; research (step 11) only has
  to fill that list.
- Mines: one per plot, on an owned plot. Capex = `mine_capex_per_hectare` x
  area x capex techs, paid up front; no opex while building. After
  `mine_build_days` the mine operates and extracts every unlocked deposit
  on the plot each day at `base_output_tonnes_per_day` (per resource) x
  output techs x (0.5 + 0.5 x remaining share), so output halves as a
  deposit nears exhaustion. Locked ore stays in the ground. Daily opex is
  `mine_opex_per_day` x opex techs; mothballed mines produce nothing and pay
  `mine_mothball_opex_mult` of it; depleted mines pay nothing.
  (Since spec 008 there is no single `mine_opex_per_day` in
  `balance.json`: each ore in `resources.json` carries its own, and a mine
  pays the highest among the ores it produced on its most recent operating
  day, through `sim::mineRunningCost`. The techs, the mothballed share and
  the depleted case are as written here. See "Markets that bite
  (spec 008)".)
- Cash may go negative from opex; bankruptcy (step 10) decides what then.
  `availableCash` is negative in that case, so no new spending passes.
- Extraction updates hidden truth directly; the owner sees only lifetime
  extracted tonnes per resource and the mine state, so remaining tonnage is
  inferred from the output decline, as the brief asks.
- Mines are public (existence, owner, state, including Depleted); surveys,
  extraction totals and capex paid are private. MineStarted is a public
  notice without an amount.
- Tech multipliers accumulate in content order so the result depends only on
  the set of completed techs, not the order they were finished.
- Quantity bounds (types.h): deposits <= 1e15 t, stocks and lifetime totals
  <= 1e18 t, |cash| <= 1e17 cents, deadlines <= kMaxDeadline. Validated on
  load so one more day of checked accounting can never overflow.
- validateWorld enforces mine conservation (lifetime extraction equals total
  minus remaining per deposit, zero for absent resources), state/day
  consistency for surveys and mines, one pending survey per company and
  plot, estimates only for present resources, prerequisite closure of
  completed techs, and reverse plot-to-mine links.

## Commodity markets (step 9)

- One market per resource, public to everyone: price, price history (one
  entry per day), and a smoothed figure for how much companies have been
  selling. The demand factor is hidden; players infer it from price.
- The world outside the game supplies exactly `base_demand_tonnes_per_day`
  at `base_price`, so with no company sales and no demand shock the price
  sits at base. Each day: demand = base demand x demand factor; supply =
  base demand + EMA of company sales (`market_supply_smoothing_days`);
  target = base price x (demand / supply)^elasticity with the ratio clamped
  to [0.2, 5]; price moves 1/`market_price_adjust_days` of the way to the
  target; price is clamped to [floor, cap] multiples of base. (Since spec
  008 the ratio clamp is read from `balance.json` as `market_ratio_min`
  and `market_ratio_max`, shipped at the same 0.2 and 5.)
- The demand factor is a mean-reverting random walk from the market RNG
  stream: factor += (1 - factor) x `demand_reversion_rate` + volatility x z,
  clamped to [`demand_mult_min`, `demand_mult_max`]. Event effects (step 12)
  multiply demand on top.
- SellOre sells at the price shown that day (yesterday's close), before the
  day's extraction and price update, so a player cannot react to a price
  they have not seen. Proceeds are checked against the cash bound; a sale
  that would overflow is rejected as SaleTooLarge.
- Elasticity is bounded to [0.01, 5] in the loader so the power law stays
  inside fixedExp's domain. Demand, supply and their ratio are kept in
  fractional tonnes (Fixed) so a five-tonne-a-day lithium market reaches its
  exact equilibrium instead of truncating to four.
- Known limitation: a stockpile sells at the quoted price however large it
  is; the impact arrives over the following days. Batching sales therefore
  beats selling daily. Storage costs are set at 5% of base price per month
  to make hoarding expensive; a per-day sale cap or intra-day price impact
  is the next lever if runner sweeps show batching dominates.
- Views carry only the last 90 days of prices (`recentPrices`); the UI reads
  the full history through `Simulation::priceHistory` for charts, so AI
  controllers do not copy years of history every day.
- With the shipped numbers one iron mine (400 t/day into a base demand of
  800) pushes the price to about 0.78x base; two mines to 0.66x; three to
  0.57x.

## Finance (step 10)

- Loans: equal monthly principal instalments (ceil(principal / term)) plus
  interest on the remaining balance at `loan_annual_rate` / 12. A company
  may borrow up to `loan_max_principal` minus what it still owes, at least
  `loan_min_principal` per loan (so tiny loans cannot dodge interest through
  rounding) and at most `max_active_loans` at once. Early repayment
  (RepayLoan) pays down principal and must come from available cash.
- Compulsory charges (opex, storage, instalments) go through `debitCash`,
  which saturates at -1e17 cents, so the sim can never produce a state its
  own loader rejects. Borrowing is rejected when it would exceed +1e17.
- Month end (every 30th day, pipeline step 6), for each solvent company in
  id order: storage charge (`storage_cost_per_tonne_month` x inventory x
  storage techs), then loan instalments, then the insolvency check. Charges
  are taken even when cash is negative; debt simply grows.
- Insolvency: a month-end with negative cash increments `monthsInsolvent`
  and emits a private warning with the months left; a month-end with
  non-negative cash resets it. When the count exceeds
  `bankruptcy_grace_months` the company is bankrupt: a public notice, all
  its mines Closed (no output, no opex), its loans written off, and every
  future command rejected. Its plots stay in its name for Milestone 1. For
  the player this is the lose condition the UI shows.
- `monthsInsolvent` and `bankrupt` are private to the company; only the
  final Bankrupt notice is public.

## Research, events and the AI (steps 11 to 13)

- Research: one project at a time per company, cost paid at start, the tech
  joins `completedTechs` after `duration_days` (pipeline step 7). Starting
  requires every prerequisite complete. A company's active project is
  private; completed techs are private too (only effects show, e.g. a
  lithium mine producing).
- Events roll every day for every definition from the event RNG stream, so
  the stream advances identically whatever is active; an event eligible by
  `min_year` and not already running starts when the roll is below
  `daily_weight`, lasts `duration_days`, then ends. Events are public
  (started/ended notices, active list in every view).
- Event effects multiply in event order and are clamped to [0.01, 100]:
  DemandMult scales one resource's demand in the price model, OpexMult
  scales every mine's daily opex (`mineDailyOpex` now takes the state;
  since spec 008 that function is gone and `sim::mineRunningCost` and
  `sim::mineOpexQuote` apply the event multiplier), and
  LandPriceMult scales the ask of plots listed while the event runs
  (relistings keep their decayed ask). Events roll at step 8, after markets
  and finance, so a new event first bites the following day.
- The built-in competitor (`HeuristicController`) is stateless: a pure
  function of CompanyView and Content, which keeps replays exact without
  persisting controller memory. Its rules, in order each day: sell stock
  when price >= 85% of base or storage exceeds 2% of stock value; mothball
  mines whose products all trade below half base and reopen above 70%;
  survey owned plots and build when the surveyed value (half the estimate at
  today's prices) covers twice the capex with six months of opex in hand;
  survey one listed plot a day when the auction leaves time for results, bid
  the ask when the surveyed value covers twice the ask plus capex and 10%
  over when it covers four times; research the cheapest available tech when
  cash exceeds 2.5M; borrow 2M chunks only while producing and below 750k.
  Its knobs lived in code until spec 006 moved them to `data/ai.json`
  (see "Balance: a contested economy").
- Hosts drive controllers with `collectCommands` before each tick; the sim
  never calls controllers itself, so the player's UI and the AI use the same
  path.

### After the Codex review of steps 11 to 13

- The AI runs every rule against one projected cash position (`Budget`), so a
  day's commands are jointly affordable; sales add projected proceeds and
  respect the cash bound. Bids and listed-plot surveys require the land plus
  the mine plus six months of opex to be affordable, so the AI cannot strand
  itself on undeveloped land. Research waits until a mine is producing.
  Surveyed value counts only unlocked resources and at most five years of
  output. Uncontested auctions get the ask, not a premium. Only active loans
  count toward the loan cap. Idle operating mines are mothballed after 30
  days.
- Event multipliers walk definitions in content order, saturate instead of
  aborting on overflow and clamp once at the end, so the result depends only
  on which events are active. The combined demand factor is bounded to
  [0.01, 1000] before it meets base demand. (Since spec 008 that bound is
  read from `balance.json` as `demand_factor_min` and `demand_factor_max`,
  shipped at the same values.)
- Validation: pending research must have been startable (prerequisites
  complete, tech unknown), projects of one company never overlap, event
  instances of one definition never overlap and respect `min_year`, start
  days are in the past, deadlines are computed in 64 bits.
- Views gained `mineCapexQuote` per plot, `mineOpexQuote` and
  `surveyNoiseQuote` per company, and `lastDayTonnes` per own mine, so the
  UI can show costs and today's output without re-deriving rules. The active
  research view no longer carries its global index, which leaked how many
  projects other companies had started. (Since spec 008 the company view
  carries `mineOpexQuotes`, one quote per ore, in place of the single
  `mineOpexQuote`; `sim::mineOpexQuote` is now the sim function that
  prices one ore for one company.)

## Balance runner (step 14)

- `sim_runner` drives every company, the player's slot included, with the
  heuristic AI, so a run is "what happens if nobody plays". Options: seed,
  days or years, number of rivals, data directory, monthly CSV, `--sanity`.
- The JSON summary reports per-company outcome (cash, debt, bankruptcy day,
  plots, mines, techs), per-market price ratios and days pinned at the
  floor or cap, rejected commands by reason, and five checks: the world
  validates every year and at the end, no company strays beyond 1000x
  starting capital, every price's mean sits within [0.3, 3] of base and is
  pinned less than a quarter of the time, the AI never issues an invalid
  command, and at least one mine is built in runs of two years or more.
  `--sanity` turns a failed check into exit code 3; CI runs five years.
- First sweep (seeds 1 to 6, two rivals, 20 years): all checks pass. The
  pattern is winner-take-all: a company that gets three or more mines going
  snowballs to 100M-600M while one- or two-mine companies went bankrupt in
  year four. The AI changes below address the losers' behaviour; the
  snowball is a balance question for the data files.
- AI adjustments from the sweep: mines are mothballed when a day's output at
  today's prices is worth less than opex (using `lastOutputTonnes`, the
  output on the most recent operating day) and reopened at 125% of opex;
  research is paid from net worth (cash minus debt); valuable plots get a
  10% or 25% premium over the ask because sealed ties go to the earlier
  bidder.
- Second sweep finding: at 12k/day opex an iron mine on a median-or-smaller
  deposit never repays land plus capex once prices sit at 0.8x base and
  output declines with depletion, so every one- or two-mine company died.
  Mine opex is now 8k/day (iron nets about 11k/day at 0.8x base, copper 46k,
  nickel 36k, lithium 62k) and mothballed mines pay 15%. (Since spec 008
  the cost is per ore, 18k to 35k a day; the mothballed share is still
  15%.) Iron stays the
  low-margin bulk resource, but a reasonable deposit now pays back.
- The AI values a prospect net of opex over the extraction time, requires
  the net value to cover land plus capex by 20%, borrows its one bootstrap
  loan only for a specific surveyed plot it could not otherwise afford, and
  repays an idle loan rather than paying interest while it waits.
- Third sweep finding (seed 8 trace): the loser's mine was fine; the market
  was not. One rival with twenty mines held copper and nickel at a quarter
  of base for years, so every small producer bled out. Base demand was
  sized for one or two mines per resource in a thirty-plot world. Changes:
  base demand doubled (iron 1600 t/day, copper 200, nickel 60, lithium 16),
  elasticities softened (0.5/0.7/0.8/1.0), and world demand now compounds
  by `demand_growth_per_year` (4%), evaluated continuously, so expansion is
  absorbed over a twenty-year game (about 2.2x by the end).

## Client (steps 15 to 18)

- Three layers above the sim: `platform/` (Services interface, NullServices
  now, Steamworks later), `game/session/` (Session: time control, player
  command binding, AI rivals, notice log, save/load, text formatting; no SDL
  or RmlUi, unit tested), and `game/` (SDL3 window and OpenGL context through
  RmlUi's reference backend, one RmlUi context, App + data model).
- RmlUi 6.3 and SDL3 come from vcpkg behind the manifest feature `game`,
  enabled by the `debug-game` and `release-game` presets. vcpkg prunes
  packages outside the active feature set, so the client has its own build
  directory. SDL3 is pinned to the x11 and wayland features: the default
  IBus feature pulls in dbus, systemd and libxcrypt (autotools) for nothing
  this game needs.
- The reference SDL + GL3 backend is vendored under `game/backends/` (MIT)
  because RmlUi ships it as sample code, not as part of the library. One
  edit: the SDL_image texture loader was removed; the interface uses no
  bitmap images. The GL3 renderer carries its own GLAD loader, so no system
  OpenGL headers are needed to compile.
- The interface is one RML document with tabs (dashboard, plots, market,
  research, finance, log) driven by a single data model. `UiModel` holds
  string-formatted copies of the CompanyView; `refreshModel` rebuilds it
  whenever a day ticks or an order is placed, and the handle marks every
  variable dirty. The RML never sees sim types.
- Orders from the interface become sim commands through the session, which
  binds them to the player; results arrive the next day as notices in the
  log and a transient status line confirms the order was queued. The sim's
  own validation is the only validation; the interface merely greys out
  buttons it knows cannot succeed.
- `<price-chart>` is a custom RmlUi element drawing the last 90 daily prices
  as columns with the base price as a reference line, from a provider the
  app installs over `Simulation::priceHistory`.
- Keyboard: space pauses, 1/2/3 set speed, period steps one day, F8 opens
  the RmlUi debugger. Time advances by whole days from real time with a
  catch-up cap of two seconds' worth, so a stalled frame never fast-forwards.
- Saves go to SDL's per-user preference directory (`autosave.dcsave`); the
  log is not part of the save and restarts with a "Game loaded" entry.
- Fonts: DejaVu Sans (Bitstream Vera licence) bundled under
  `game/assets/fonts/`.
- This sandbox has no X11 or OpenGL headers, so the client is compile-checked
  locally with SDL3's dummy video driver and run on the developer's machine;
  CI builds it on Ubuntu (with the X11/Wayland dev packages) and Windows.

### After the Codex review of the client

- Default bids are prefilled as exact text (`exactMoney`), never a rounded
  label. Typed amounts go through strict parsers in the session layer
  (`parseMoney`, `parseTonnes`): the whole text must be consumed, money is
  parsed into integer cents, signs are preserved so the sim rejects negative
  orders with its own reason. Both are unit tested.
- The session reads only the view (day, company count) and exposes
  `priceHistory` and `stateHash`; the app no longer touches the Simulation.
- The sell handler forwards whatever quantity parses; the sim decides.
- Saves are written to a temporary file and renamed into place after a
  successful close. SDL's UTF-8 paths are converted explicitly on the way
  into std::filesystem and back out to RmlUi.
- Assets are copied by an always-run target so .rml/.rcss/.json edits reach
  the build tree without a C++ change.
- The dashboard opens with a dismissible how-to-play card, the header shows
  spendable cash next to cash (labelled "Available" and net of pending
  orders since spec 007), finance shows loan terms and insolvency
  progress, status lines say "queued" and point to the log for rejections.
- First test-play finding: every block in the content panel laid out at
  zero width. Cause (RmlUi): a scroll container subtracts its vertical
  scrollbar's width from the children's containing block, and an unstyled
  scrollbar element is as wide as its parent. Scrollbars are now styled.
  `deep_claim_layout_probe` loads the real interface with a stub renderer
  and prints computed boxes, and runs as a test in the game presets so a
  collapsed layout fails CI.

## Monitored playtesting: trace, screenshots, replay

The client cannot be watched directly from the development sandbox (no
display there), so it leaves a trail that can be read, and reproduced, from
the files it writes. Everything lives in `game/session` except the two
pieces that need OpenGL.

- **Trace** (`game/trace.h`): one JSON object per line, flushed per line so
  a crash keeps the record. Records: `new_game` (seed, player, AI names,
  content hash, build id), `load` and `save` (with a copy of the save bytes
  written next to the trace, because the real save lives in the pref path
  and may be overwritten), `command` (the player's order, written when the
  session hands it to the sim and tagged with that day, which is the day it
  was given), `day` (day and state hash after every tick),
  `notice` (each player-visible log entry with its text), `state` (monthly
  summary: cash, reserved, debt, capacity, plots, mines with output, stock,
  prices, rivals), `ui` (tab, select_plot, select_resource, speed, status,
  and since spec 007 order_queued and order_cancelled),
  `marker`, `screenshot` and `end`. Seeds travel as decimal strings and
  hashes as hex strings so JSON tools that read numbers as doubles do not
  corrupt them. The trace is written by the session (orders, ticks,
  notices, monthly state) and the app (new games, loads, saves, interface
  events); the sim knows nothing about it.
- **Where**: the `DEEP_CLAIM_PLAYTEST` environment variable if set (`off`
  disables tracing); otherwise `playtest/` in the repository for a
  development build (gitignored); otherwise `playtest/` under the per-user
  data directory. The build id is the short git revision, read on every
  build since spec 007 (it was captured at configure time before).
- **Screenshots**: F9 writes a `marker` record and takes a screenshot, F12
  takes one. The frame is read back from the default framebuffer in a
  pre-swap callback added to the vendored backend (the only other edit
  besides the SDL_image removal), after RmlUi's EndFrame has composited
  the layers into the back buffer and before SDL swaps it. The PNG encoder
  (`game/png.h`) writes 8-bit RGB with stored deflate blocks, so there is
  no image or zlib dependency; a 1366x820 frame is about 3.4 MB and a
  session is capped at 200 shots.
- **Replay** (`game/replay.h`, `deep_claim_replay`): runs the same Session
  the client ran from the recorded seed and setup, submits each order on
  its recorded day, ticks once per `day` record and compares the state hash,
  so the first day where the replay stops matching the client is reported.
  `--day N` stops early and prints the player's full view at that day,
  `--save` writes a `.dcsave` of it, `--log` prints the player's log. It is
  built in every preset (no client dependencies) and the trace round trip,
  divergence detection, stop day and load-following are unit tested. The
  headless layout probe also checks that the app writes a trace header.
- Rejected: tracing from inside the sim (it must stay free of I/O) and a
  per-frame screenshot stream (too much data for too little signal).
- Playtest sessions are written up under `docs/playtests/`, one file per
  session, with the findings ranked and the order of work they imply. The
  first is `docs/playtests/2026-10-08.md`.

## Spec-driven development

From spec 001 onwards every feature starts as `docs/specs/NNN-name.md`
(goal, player-facing behaviour, sim rules, view, client, tests, out of
scope, decisions), approved before code is written. The spec is committed
first, the implementation follows it, and the Codex review is run against
the spec as well as the code. Decisions made during implementation are
appended to the spec's Decisions list, and this file keeps the
architectural summary.

## Standing sell policies (spec 001)

- Each company holds one `SellPolicy {enabled, reserveTonnes, floorPrice}`
  per resource plus a `coverCosts` flag, set by the `SetSellPolicy` and
  `SetCoverCosts` commands and seeded from `balance.json`
  (`default_sell_policy`, `default_cover_costs`; both on in the shipped
  data because unsold ore was the dominant failure in the first playtest).
- Pipeline step 4b, `runSellPolicies`, runs after mines and before markets:
  an enabled policy whose floor is met sells everything above the reserve
  at today's price through the same path as a manual order
  (`executeSale`, bounded by `saleTonnesWithinBounds`). No notice per sale;
  month-to-date totals accumulate in the company and become one
  `AutoSaleSummary` notice per resource at month end.
- Month end (`runFinance`): after storage and loan charges and before the
  solvency check, a company with `coverCosts` and negative cash sells just
  enough of its most valuable stock to reach zero (`CoverCostsSale`
  notices), then the summaries are emitted and the totals reset.
- The AI controller is unchanged for now; it keeps its own daily rule.
- The golden determinism hash moved once (company state grew).

## The prose catalogue (spec 002)

- Every sentence a player reads lives in `data/strings.json`, a flat
  `key -> template` object with named `{placeholders}`. The loader holds a
  static table of every fixed key and the placeholders it may use, derives
  the per-id keys (`event.<id>.name|description`, `tech.<id>.name|description`)
  and the `reject.<reason>` keys from the data and the sim's enums, and
  reports a missing key, an unknown key, an empty text, malformed braces
  or a placeholder the key may not use as an ordinary content error. A new
  `NoticeKind` without prose fails at load.
- `Content::strings` is deliberately outside `serialize(Content)` and
  therefore outside the content fingerprint: prose changes no simulation
  state, and a wording fix must never invalidate a save. Event and tech
  names stay in their defs (and so in the fingerprint, exactly as before)
  but are filled from the catalogue; descriptions are new and also outside
  the hash.
- `game::fill` substitutes named placeholders and leaves unknown ones
  visible; `game::text` returns `[key]` for a missing key so in-code test
  content still produces readable output. `describeNotice` and
  `rejectReasonText` choose keys and supply figures; they contain no
  sentences. The client binds `ui.*` strings into the document through the
  data model and sets status lines from `status.*`.
- The pipeline is `tools/prose/draft.tsv` (source of truth for meaning) ->
  `polish.sh` (agy, voice brief, invariant check) -> `polished.tsv` ->
  `to_json.py` -> `data/strings.json`. All three files are committed; a
  test keeps the draft and the catalogue in step on keys and placeholders.
- Rejected: a built-in English fallback in C++ (it would hide a missing
  key and keep prose in code), and loading strings before the other files
  (the per-id keys need the ids first).

## Client restyle (spec 003)

- The client is laid out from `docs/design/handoff.md` (plate C "Control
  Room", dashboard H4): header with a 90-day cash sparkline, a status strip
  on every tab, a 56dp icon rail that expands to 160dp on hover, title
  blocks with mono indices on every section, and the five tabs in the
  hand-off's block order. Colours and sizes are the literal token values;
  `tools/design/tokens_to_rcss.py` regenerates `game/assets/ui/tokens.rcss`
  from `docs/design/tokens.css` as a lookup table and a test keeps them in
  step. Fonts are Barlow, Barlow Condensed and JetBrains Mono (OFL, in
  `game/assets/fonts/` with licences); every figure is set in the mono face
  because RCSS has no tabular-figures property.
- RmlUi 6 translation rules, applied throughout `main.rcss`: no custom
  properties (literals), no `gap` (margins on children), no `min-width: 0`
  on flex children (explicit `width` with `flex: 1 1 0`), chips are
  `inline-block` with a fixed height and line-height, selected rows use a
  fill, `:hover` on the rail drives the label rule, icons come from a
  `@spritesheet` over `icons.tga`, every length is in dp. Every flex child
  that carries text is `display: block`: an inline span ignores its width
  and overflows. Utility class names must not collide with table rows: a
  `.row { display: flex }` once turned `tr.row` into an invalid table child
  (RmlUi drops it with a warning), so the inline flex helper is `.hrow`.
- `body.deck` is set by the app below 1300dp of window width or with
  `--deck`: 40dp controls and inputs, 14dp minimum text (one rule lists
  every 12dp and 13dp class), short button labels through `.long`/`.short`
  spans, the strip's research item on a second line, a 320dp side column.
- The view model (`game/src/ui_model.*`) is presentation only: needs-you
  rows are built from view data and sorted warnings first then by day;
  "days remaining" is best survey estimate less extracted over the last
  output; an idle mine is an operating mine past its opening day with zero
  output everywhere; the storage warning is storage above a tenth of cash;
  with one rival, every bid that is not the player's is that rival's. All
  sentences come from the catalogue; chip words, column heads and button
  labels stay literal.
- Auto-pause is a session rule set (`game::AutoPause`, eight `PauseOn`
  reasons, defaults all on except survey complete and mine operational).
  `Session::advance` stops at the first pausing notice and records it;
  the app shows it on the status line through `status.paused_by` and keeps
  the toggles in `autopause.txt` next to the save.
- Sim additions are notices only: `IdleMineWarning` and `MineRunningLow` at
  month end, `MineFoundNothing` on an opening day with nothing extractable.
  The mine state machine and the content fingerprint are unchanged;
  `MineView::bestEstimateTonnes` exposes the owner's latest survey so the
  client needs no rules of its own.
- The layout probe (`deep_claim_layout_probe`) is the regression test for
  the stylesheet: every tab at 1366x820 and 1280x800, failing on a
  collapsed content pane, horizontal overflow past the nearest scroll pane,
  text under 14dp in the deck frame, and any RmlUi warning or error (an
  unbound `{{variable}}` or unknown property shows up there and nowhere
  else). The stub texture loader reports the icon sheet's size so sprites
  resolve.
- Tables: RmlUi gives unspecified columns equal shares and never widens a
  column for its content. Each table fixes every column but one or two on
  the header cells (`th.w-*`), and the free columns wrap. The probe checks
  this directly: a no-wrap text wider than its block or cell is a failure,
  and each frame is probed on a fresh game and on a fixed-seed game played
  to a working mine with a loan, research and the auto-pause menu open.
- Remaining ore: `sim::estimatedRemainingTonnes` is the one definition,
  used by the view and by the running-low notice. A survey estimates what
  was in the ground when it completed, so only later extraction is
  subtracted; with no stored baseline that is exact for surveys from
  before the mine opened and approximate (last output times days since)
  for later ones.
- Rejected: per-row standing-order forms on Market (inputs are two-way
  bound to one model string each; one form for the selected resource
  instead), CSS ellipsis for the company name (truncated in code), SVG
  icons through the RmlUi SVG plugin (adds lunasvg; a TGA sprite sheet is
  enough for six icons).

## Playtest 2 fixes (spec 004)

Sim:
- **Unsold land is withdrawn.** A plot counts its unsold rounds
  (`Plot::unsoldCount`). At `balance.max_unsold_rounds` (3) it leaves the
  market instead of relisting: listing Unlisted, `relistNotBefore` = close
  day + `balance.relist_cooldown_days` (360), appended to
  `WorldState::withdrawnQueue`, public `PlotWithdrawn` notice. The monthly
  lister offers never-listed plots first; when none remain it takes the
  front of the withdrawn queue once its cooldown has passed. Only the front
  is checked: cooldowns end in withdrawal order. A returning plot keeps the
  reduced ask it left with and starts a fresh count. `listingQueue` stays a
  permutation of all plots, so "behind never-listed plots" is structural
  rather than a sort. `max_unsold_rounds` 0 keeps the old relist-forever
  rule; the in-code test fixture uses it.
- **The golden determinism hash moved once**, 0x0fc2e895ca56a363 to
  0x28adea3aad5623a7: plot state grew by two fields, world state by the
  withdrawn queue and balance by two parameters, all of which are hashed.
  No rule that the fixture exercises changed. (`Mine::lowWarnedSurvey` is
  hashed too; the golden game has no mines, so it did not move the value.)
- **Rival unlocks are news.** `runResearch` emits the public
  `RivalUnlockedResource` (company, tech, resource) once per resource a
  completed tech unlocks. The owner still gets its private
  `ResearchComplete`; the session drops the player's own unlock line.
- **Running low uses the low end.** The month-end check works on
  estimate x (1 - relative error), minus extraction since the survey, and
  warns when that lasts 30 days or fewer (the notice's day count is floored
  at zero). It speaks once per survey: `Mine::lowWarnedSurvey` records the
  survey already warned about, so mothballing, a new unlock or a change of
  output cannot repeat it, and a survey that is already past its low end
  when the mine opens still gets its one warning. A newer survey starts
  afresh. The old rule repeated every month and waited for the central
  estimate.
- **View.** `unlistedPlotsRemaining` (never-listed plots), `withdrawnPlots`,
  and per resource `unlockDaysRemaining` (days until the ore is mineable
  when the unlocking tech is in the lab: completion day - today + 1) and
  `unlockReady` (prerequisites complete, tech neither done nor running).
- Saves validate the new state: a mine's warned survey is a completed
  survey of its plot by its owner; counts only on listed plots and under the
  limit, cooldowns only on withdrawn plots, the withdrawn queue distinct,
  unlisted and in nondecreasing cooldown order, and every offered plot
  that is unlisted and unowned accounted for by that queue.

Session:
- **The log is the player's, not the world's.** `Session::logNotices` drops
  `PlotRelisted` and `PlotWithdrawn` for plots the player has neither
  surveyed nor bid on, drops the player's own `RivalUnlockedResource`, and
  folds a tick's `AutoSaleSummary` notices into one line built from
  `notice.auto_sale_month` and `notice.auto_sale_item`. The trace's notice
  records are written from the same place, so they are what the log shows;
  day and hash records are untouched and replay is unaffected.
- **Auto-pause** gains `research_complete`. Survey and research complete
  default to on. The toggles file is read over the defaults, so a reason the
  file has never heard of takes its default and every recorded toggle stays.
- **Text.** Rejection templates embed `rejectReasonClause` (no full stop,
  lower-case opening); `describeNotice` capitalises its result.
- **Trace.** `StartResearch` command records carry `tech_id`; decoding still
  uses the index. `deep_claim_replay` prints "building, opens day N" and a
  bare "depleted".

Client (all presentation of view data):
- Finance: Repay all per loan; the loan form shows and prefills remaining
  credit and disables Take loan with the reason.
- Needs you: "The lab is idle" (when a project could start now) and "Survey
  report on plot #N is in" (15 days, while the plot is listed).
- Plots: a warning when a survey would report on or after the auction's
  closing day; no survey on a worked-out mine; locked ore reads "mineable in
  N days" or "one project away" with buy-ahead advice once
  `unlockDaysRemaining` or `unlockReady` says so; a line when no unlisted
  land remains.
- Log: All / Mine / Market / Rivals, classified in `ui_model.cpp` from the
  notice kind and company.
- The layout probe plays its fixed game on through a landed survey report,
  the end of the land queue, exhausted credit and a late survey, lays out
  every tab at each stage, and lays out the Log tab under every filter on
  both a fresh and a long game.
- Observation from the probe's fixed game, not acted on here: by year 3,
  23 of 30 plots had been withdrawn unsold. Land demand is spec 005/006
  territory.

## World and opening (spec 005)

- **Size and pace.** 40 plots per region (120), four listed at the start,
  one a month: new land for 116 months, the last on day 3481. The first
  draft of the spec said 30 per region; the runner showed that world out
  of unlisted land on day 2581, short of its own year-8 goal.
- **The listing queue is no longer a plain shuffle.** World generation
  classifies each region for a company with no research
  (`classifyRegionAtStart`): Mineable when an ore that needs no research
  is present on at least half the plots, Locked when it is not Mineable
  and holds an ore that does, Other otherwise. The queue is an opening
  hand (`plots_listed_at_start` plots, at least `opening_min_mineable`
  from Mineable regions, at most `opening_max_locked_region` from Locked
  ones, shuffled) followed by every other plot in random order with no
  third plot in a row from one region. Only the world RNG stream is used.
- **Regions and odds, never the ground.** A plot from a mineable region
  can still survey empty. Guaranteeing actual ore would leak hidden
  deposits through the public queue and make surveys pointless.
- **Rules give way, they do not abort.** A hand rule is dropped when no
  remaining plot can meet it, and the run rule when every remaining plot
  is in the blocked region, so any content generates. The in-code fixture
  (20 mineable, 10 locked) ends its queue with a forced run of North; the
  property test accepts a run of three only when nothing else was left.
- **Golden hash.** `0x28adea3aad5623a7` to `0x118a72ce0f57d9f7`, once,
  because the fixture's queue is drawn differently. No state was added.
  The content fingerprint also changes (two balance fields, plot counts),
  so saves and traces from before this spec do not load.
- **Unsold land.** `max_unsold_rounds` 6 and `relist_cooldown_days` 180
  (owner's choice after spec 004). Even so a median of 44 of 120 plots are
  withdrawn at year 8: two companies do not absorb a plot a month.
- **Runner sweep.** `sim_runner --sweep N` plays N seeds and prints, per
  seed and as min/median/max, the opening hand, the queue's longest
  region run, the land horizon and withdrawn land at years 2, 5 and 8,
  company 0's first mine, and final cash, then the spec's goal checks.
  The opening and year-8 checks gate the exit code and run in CI as
  `runner_sweep` (ten seeds, eight years). The first-mine check is printed
  with `"gated": false`: the AI opens its first mine on day 273 at the
  median against a goal of 250, and changing the hand does not move it.
  That, and the AI ending below zero in 5 of 50 seeds in the larger world,
  are for spec 006.

50 seeds, 10 years, one rival, release build:

| Per seed | min | median | max |
| --- | --- | --- | --- |
| Opening plots in a mineable region (of 4) | 3 | 3 | 4 |
| Opening plots in the locked region | 0 | 1 | 1 |
| Longest same-region run, monthly listings | 2 | 2 | 3 |
| Day the last unlisted plot was listed | 3481 | 3481 | 3481 |
| Unlisted plots at year 2 / 5 / 8 | 93 / 57 / 21 | 93 / 57 / 21 | 93 / 57 / 21 |
| Withdrawn plots at year 2 | 6 | 10 | 15 |
| Withdrawn plots at year 5 | 23 | 28 | 37 |
| Withdrawn plots at year 8 | 35 | 44 | 57 |
| Day company 0 started its first mine | 47 | 93 | 423 |
| Day company 0's first mine opened | 227 | 273 | 603 |
| Final cash, company 0 | -$0.58M | $187M | $1,086M |
| Final cash, company 1 | -$1.01M | $831M | $1,219M |

## Balance: a contested economy (spec 006)

- **The competitor's knobs are content.** Sixteen constants left
  `ai_controller.cpp` for `data/ai.json` (`Content::ai`, `AiParams`),
  joined by three new ones; the "mineable region" threshold left
  `worldgen.cpp` for `balance.json` (`opening_mineable_presence`). The
  controller runs inside the deterministic sim, so all of it is hashed
  into the content fingerprint: a changed knob invalidates saves and
  traces exactly as a changed price does. `data/` now holds seven files.
- **Bid offsets were biased, and that was the runaway.** The per-plot
  offset meant to stop identical AIs tying was `(plot * 7 + company * 13)
  % 7`; the plot term vanishes, so company 1 outbid company 0 by six
  points on every plot. It is now a hash of plot and company
  (`aiBidJitterPercent`). This one line took the leader's lead from 3.06x
  to 1.27x and the AI-played company's first mine from day 273 to 228,
  and it explains the rival's 16% and 32% winning premiums in both
  playtests (10 + 6 and 25 + 6). Rival premiums now vary from 10 to 16%
  and 25 to 31%.
- **Valuation is marginal revenue.** `aiValuedPrice`: today's price, never
  above base, moved by the market's own rule for the new mine's output
  (`(before / (before + added)) ^ elasticity`), with the same drop charged
  on what the company's operating mines already sell. A company with four
  mines of an ore values a fifth far below a newcomer. `supply_discount`
  scales it (shipped at 1). Player and AI share the market rule; only the
  AI's appetite changed.
- **First-mine terms.** A company with no mine uses
  `first_mine_required_return` and `first_mine_recoverable_share` (1.1
  and 0.6). Measured to make no difference once bids are fair; kept as
  knobs.
- **No wind-down rule.** The sim has no order that closes a mine, and
  adding one for the AI alone would be a control the player lacks. It was
  conditional on what killed trailing companies; that turned out to be
  the bid bias and iron's margin, and with both fixed no company fails in
  150 seeds.
- **Deposits end.** `DepositPrior::tonnesMaxMult` caps a deposit at a
  multiple of its median after the plot's area has scaled it. Shipped:
  spread 1.6 and a cap of 4 on every deposit, medians raised about 1.4x
  (the narrower spread alone shortened the typical mine). The largest
  Copper Belt find is 600k t; playtest 2 drew 1.59M t. In-code fixtures
  keep the default cap, which never binds.
- **Iron at 90.** Revenue over opex at base goes from 3.0 to 4.5 (a data
  test computes it from the shipped files). This, not an AI rule, is what
  ended the bankruptcies: a company with one or two iron mines could live
  at 60 but never save for another plot.
- **Sales are public history.** `Auction::askPrice` records the ask an
  auction opened with; auctions are never erased, so that is the whole
  history (no second list). `CompanyView::recentSales` is the sixteen
  newest settled auctions with the premium over the ask, `PlotView::sale`
  a sold plot's own, and `SurveyEstimateView::major` marks an estimate
  above twice the regional median. The Plots tab shows the newest five
  sales under the for-sale table. The record reads the auctions newest
  first, so load validation now requires them in opening and closing
  order (true of every generated state; the Codex review asked that it be
  enforced).
- **Golden hash.** `0x118a72ce0f57d9f7` to `0x2a5da3177ab7c69d`, once,
  for the auction field. Save format v1 gains it; the content fingerprint
  changes as well (AI knobs, deposit caps, prices), so saves and traces
  from before this spec do not load.
- **The sweep reports the contest and gates it.** `--sweep` adds mines
  built per company at years 2, 5, 8 and the end, mines by state,
  bankruptcy days, first depletion, the lives of mines opened at least
  seven years before the end, and mean price over base. `runner_sweep`
  (ten seeds, eight years) now fails on a median first mine at day 250 or
  later, more than one bankrupt seed in fifty, a median leader-over-trailer
  ratio above 3.0x, or a seed with no depleted mine by year 8. Mean
  prices, mine life and withdrawn land are printed with their goals and
  do not gate.
- **What is not solved.** Ore trades at 0.50 to 0.57x base once two
  companies are established, against a goal of 0.75x, and 37 of 120 plots
  sit withdrawn at year 8 against a goal of 25. One copper mine is 30% of
  base copper demand and still earns twice its opex at a third of base,
  so two rational companies build until prices are near there; the AI's
  knobs barely move it. Tripling every base demand gets prices to 0.68 to
  0.79x. Market size was outside this spec.

50 seeds, 10 years, one rival, release build (seeds 51 to 150 in the last
column were never used for tuning):

| Measure | Spec 005 | Target | Seeds 1 to 50 | Seeds 51 to 150 |
| --- | --- | --- | --- | --- |
| Median day company 0's first mine operates | 273 | under 250 | 228 | 230 |
| Seeds with a bankrupt company | 6 of 50 | at most 1 in 50 | 0 of 50 | 0 of 100 |
| Mines built, leader over trailer, median seed | 3.06x | at most 2.5x | 1.08x | 1.08x |
| Mean price over base: iron, copper, nickel, lithium | 0.61 0.53 0.66 0.99 | 0.75 or more | 0.57 0.50 0.54 0.96 | 0.57 0.50 0.56 1.00 |
| Median life of a mine opened in the first three years | 1,789 days | 1,800 to 2,520 | 2,167 | 2,355 |
| Seeds with no depleted mine by year 8 | 0 | none | 0 | 0 |
| Median withdrawn plots at year 8 | 44 | 25 or fewer | 37 | 38 |
| Iron revenue over opex at base | 3.0 | 4.5 | 4.5 | 4.5 |
| Mines built by year 10, company 0 / company 1, median | 16 / 49 | | 37 / 37 | 37 / 37 |
| Final cash, company 0 / company 1, median | $187M / $831M | | $765M / $832M | $764M / $818M |

## Pending orders (spec 007)

- **Orders wait in the session, not the sim.** `Session::submit` binds the
  order to the player, gives it an id and appends it to a queue of
  `PendingOrder {id, command}`; `pendingOrders()` returns the queue in the
  order given. `giveOrders` hands the whole queue to `Simulation::submit`
  in that order at the top of `tickOnce`, before the rivals decide, and
  nowhere else. Any tick empties the queue, so an order reaches the
  sim on the day it was given and the game plays out exactly as if it had
  gone straight there. Ids start at 1 and are never handed out twice in a
  session.
- **Cancel is not a sim rule.** `cancel(id)` and `cancelAll()` erase from
  the session's queue; the sim never saw the order, so there is nothing to
  validate, charge, log or undo, and no command, notice or state was added.
  `cancel` returns false for an id that is no longer waiting (given or
  cancelled already), and the client then sets no status line. Taking back an
  order the sim already holds would be a sim rule and is out of scope.
- **A save carries the orders and leaves them waiting.** `Session::save()`
  is const. It copies the sim, submits the queue to the copy in the order
  given and saves the copy, so the save holds the orders as the current
  day's commands while the session's queue is untouched: the orders are
  still listed, still counted in Committed and still open to Cancel. A
  save writes no `command` record, and a game that saves plays out exactly as
  one that never does. With an empty queue the bytes are the sim's own
  save. The copy is the cost, paid on a rare act. The build first handed
  the queue over inside `save()`; review found that a save with the clock
  stopped then removed the chips and gave Available back while the money
  was already spoken for. A successful `load` clears the queue. The
  orders in a loaded save are in the sim, not the queue, so they are not
  shown as waiting and cannot be cancelled: an order cancelled after a
  save comes back if that save is loaded.
- **Trace.** A `command` record is written at hand-over, not when the
  button is pressed, with the hand-over day (the day the order was given).
  A cancelled order therefore never appears as a command. `submit`,
  `cancel` and `cancelAll` write `ui` records of kind `order_queued` and
  `order_cancelled`, whose detail is `#<id> <command json>`; the id ties a
  cancellation to the order it withdrew. `kTraceVersion` is still 1.
  Replay feeds each `command` record to `Session::submit` and ticks on
  each `day` record, so it goes through the same queue and needs no rule
  for cancelled orders.
- **Committed and Available are display figures (R-001).** `orderCost`
  prices one waiting order at today's quotes: the survey fee, a bid less
  the player's standing bid on that plot (the view already counts the
  standing bid as reserved, and the sim asks a rebid to cover only the
  difference), the plot's `mineCapexQuote`, the tech's cost, a repayment's
  amount; loans, sales, mine switches, sell policies and cover-costs cost
  nothing. `committed()` sums it over the queue and `pendingBorrowed()`
  sums the principal of waiting loans. Both sums, and the bid difference,
  saturate at the largest amount: an order can carry any amount until the
  sim turns it away, and `Money`'s checked arithmetic ends the program on
  overflow. The client's one affordability
  figure is cash - reserved - committed + pending borrowed
  (`Orders::available` in `ui_model.cpp`); the header shows it as
  "Available", where it used to show "Spendable", and the status strip
  shows Committed while it is above zero. None of this is a rule: the sim
  validates every order when it arrives and its rejection is what counts.
- **Display costs are never negative, and unknown things cost nothing.** A
  cost is clamped at zero so an order with a nonsense amount cannot make
  money look available, and a survey, bid, build or research order naming
  a plot the view does not show, or a tech the content does not hold,
  costs nothing. Such orders stay in the
  queue for the sim to reject.
- **"Not covered" is a walk, not a rule.** `readOrders` goes through the
  queue with a running balance that starts at cash less reserved: a loan
  adds its principal, any other order is flagged when its cost is above
  zero and above the balance, and only a covered order is subtracted. An
  uncovered order takes nothing from the orders after it, as a rejected
  order would not, so cancelling a loan does not flag everything behind
  the first shortfall.
- **One order path in the client.** Every handler goes through
  `App::order`, which submits and refreshes at once. Before, an order
  given with the clock stopped changed nothing on screen, because the
  interface shows sim state and the sim had not applied the order; now
  the queue itself is shown. Cancel sets `status.order_cancelled`. A
  second click on the cover-costs switch while its change is waiting
  cancels that change instead of queueing its opposite behind it.
- **Strings.** Chips and pending lines are `ui.pending_*` catalogue keys.
  Sell-policy and cover-costs changes needed labels the spec had not
  listed: `ui.pending_policy_on`/`off` and `ui.pending_cover_on`/`off`.
- **Build id at build time.** The root `CMakeLists.txt` defines an
  always-run target `dc_build_id` that runs `cmake/BuildId.cmake` in
  script mode (and once at configure time, so the header exists for tools
  that read `compile_commands.json` without building). The script writes `generated/dc_build_id.h` in the build
  directory, defining `DC_BUILD_ID`, and rewrites it only when the content
  changes, so a build with no new commit recompiles nothing. The id is the
  output of `git rev-parse --short HEAD`, or `unknown` when
  `<source>/.git` does not exist, git is missing, or the output is not a
  hex string. The existence test stops a source archive unpacked inside
  another repository from taking that repository's revision. Only
  `game/src/main.cpp` includes the header, so only it recompiles after a
  commit. Playtest 3 had found the trace naming the previous merge.
- **The save reader, the one change under `sim/`.** `ReadArchive` bounds a
  vector's length prefix by the smallest serialized size of an element,
  measured from a default-constructed value. For `sim::Command`, a
  variant, the default is its first alternative, `BidOnPlot` (17 bytes
  with the tag), so the reader demanded 17 bytes per queued command and
  could refuse a valid save whose queue held a shorter one: a survey,
  build, mine switch, loan, research or cover-costs order. The bug
  predates this spec; saving with orders waiting made it easy to reach.
  `minSerializedSize` in `sim/include/sim/archive.h` now gives a variant
  its smallest alternative plus the tag (6 bytes for `SetCoverCosts`).
  Read side only: the bytes written are the same.
- **Nothing moved.** The golden determinism hash is still
  `0x2a5da3177ab7c69d`, `kSaveVersion` is still 1 and the save format is
  byte for byte what it was; no sim state, command, notice or balance
  number was added, and the content fingerprint is untouched by this
  spec's code (the new strings are outside it).
- **Open.** Listed in the spec under "Open after build"; the review
  outcomes are in the spec's Decisions under "Reviews". The feature has
  not yet been seen on a display, run through CI or playtested.

## Markets that bite (spec 008)

The spec is `spec/markets-that-bite.md`; its decision records are
`docs/adrs/F-2026-10-10-markets-that-bite-001` to `-004`. Statements in
records 002 and 003 did not survive the build. Accepted records are not
edited, so record 004 amends them: the runner does not call the margin
function, the golden hash did not move, the 60-second ceiling became a
180-second timeout, and "idled" means mothballed and then reopened. The
detail is below. The review findings and their outcomes are in the spec
under "Reviews".

- **Why price never bit.** A mine cost 8,000 a day whatever it dug. At
  base output that is 22% of base price for iron (8,000 / 400 t / 90), 15%
  for copper, 18% for nickel and 11% for lithium, against a price floor of
  10%. In three playtests companies supplied five times base demand,
  prices settled at 0.35 to 0.5 of base and every mine still paid, so
  nothing stopped either company building and the first good mine made
  money irrelevant.
- **Running cost follows the ore.** Each ore in `data/resources.json`
  carries `mine_opex_per_day` (`ResourceDef::mineOpexPerDay`); the single
  figure in `data/balance.json` is gone, and the loader refuses it there as
  an unknown key and refuses a missing, zero or negative cost on an ore.
  `sim::mineRunningCost` charges the highest cost among the mine's cost
  ores, not the sum, times the owner's research multiplier and the active
  event multiplier, times `mine_mothball_opex_mult` when mothballed, and
  nothing while building, depleted or closed.
- **Cost ores come from what the owner has seen.** They are the ores the
  mine produced on its most recent operating day (`lastOutputTonnes`).
  When there are none (a mine that has not produced yet) they are every
  ore the owner has unlocked. The plot's true deposits are never read: a
  cost must not reveal an ore the owner has not found. So the cost falls
  the day after an ore runs out and can rise the day after research
  unlocks a dearer one, and a mine's first day is charged for the dearest
  ore its owner can dig. A mine digging two ores pays one cost for two
  revenues, which makes mixed plots worth more than before.
- **One margin function.** `sim::mineDailyMargin` is the mine's most
  recent operating day's output at a given set of prices, less its running
  cost as an operating mine; `sim::mineBreakEvenPrice` is the price of its
  principal ore (largest output times price, lowest id on a tie) at which
  that margin is zero with the other ores held where they are, rounded up
  to a cent, and zero when the other ores cover the cost or there is no
  output to price. Both are pure functions of content, state and prices,
  and every step saturates at the cash bound instead of aborting. The
  mines step calls the margin with the price it sees (yesterday's close,
  before the day's market update); the view calls both with the current
  price when it is built. The AI and the client read the view and work
  nothing out: the AI idles an operating mine past its grace period when
  `dailyMargin` is negative (or it has no earnings to show) and reopens a
  mothballed one when the margin reaches `reopen_margin` times
  `operatingOpex` less that cost.
- **Correction to record 002 (now in record 004): the runner has its own
  measure.** The record says the runner calls the margin function each
  day. It does not.
  Its bite measure asks whether one mine at base output, with no research
  or event multiplier, would take less than the ore's cost at the day's
  price (`belowBaseBreakEven` in `sim_runner/main.cpp`): price times
  `base_output_tonnes_per_day` below `mine_opex_per_day`. That is the
  data's own break-even, the same for every seed, which is what a gate on
  the data should measure. No mine's margin enters the runner.
- **View.** `MineView` gained `dailyMargin` and `breakEvenPrice`, and two
  fields beyond the design: `operatingOpex` (what the mine costs, or would
  cost, running; `dailyOpex` stays the cost in its present state) and
  `hasEarnings` (operating or mothballed with output on record; when false
  the two figures are zero and mean nothing). For a mothballed mine the
  margin is what it would earn if reopened. `CompanyView::mineOpexQuote`
  became `mineOpexQuotes`, one per ore, quoted whether or not the ore is
  unlocked.
- **The only new saved state** is two fields per mine: `lossDays`
  (uint32, consecutive days at a loss) and `lossAnnounced` (uint8),
  serialized after `lowWarnedSurvey`. A mine counts only while it is
  operating, its owner is not bankrupt and it has output to price. A day at
  break-even or better sets `lossDays` back to zero. `lossAnnounced` is
  cleared only by a profitable day or a day the mine does not count; a day
  at exactly break-even leaves it set (changed after the Codex review,
  finding 1). On load a save is refused when `lossAnnounced` is above 1,
  or when `lossDays` is not zero on a mine that is neither operating nor
  closed. Closed is allowed because the game itself writes a closed mine
  with a count on the day its owner goes bankrupt. Margin and break-even
  are never stored, so nothing derived can go stale in a save.
  `kSaveVersion` is still 1.
- **The losing-mine notice.** On the day `lossDays` is at or past
  `mine_loss_notice_days` (`balance.json`, shipped at 5) and the run has
  not been announced, the owner gets one private `MineLosingMoney` notice
  carrying the day's loss as a positive amount and the count of days, and
  `lossAnnounced` is set. "At or past", not "equal to", so a save that
  carries a longer run not yet announced is told on its next losing day
  instead of never. Nothing more is said until the mine has had a
  profitable day, or has stopped operating, and has then run at a loss for
  the set number of days again. A mothballed mine never
  triggers it. Because the counter runs one market update behind the
  figure on screen, the two can disagree for a day around zero; the run of
  consecutive days absorbs that.
- **The stop rule.** `game::PauseOn::MineLosing`, key `mine_losing`, maps
  the notice to an auto-pause reason when it is the player's own mine. It
  is on by default and listed with the other stop rules; a settings file
  from before it existed takes the default and keeps its other toggles.
- **Market clamps are data.** The two clamps that were literals in
  `sim/src/markets.cpp` are `market_ratio_min`/`max` (demand over supply,
  0.2 and 5) and `demand_factor_min`/`max` (walk times growth times
  events, 0.01 and 1000) in `balance.json`, shipped at the values the
  code had. The loader bounds the ratio pair to [0.01, 10] and the factor
  pair to (0, 1000], each minimum strictly below its maximum: a ratio of
  10 keeps base price times ratio^elasticity inside int64 cents at the
  largest elasticity and price the loader allows, and 1/100 keeps the
  exponent inside `fixedExp`'s range. The ratio pair must also straddle
  1: the minimum may not exceed 1 and the maximum may not fall below it
  (added after review). Base price is the price at a ratio of 1, and the
  AI clamps its supply discount between the minimum and 1, which a
  minimum above 1 would turn into a premium.
- **Content fingerprint.** The per-ore cost, the four clamps and
  `mine_loss_notice_days` are serialized with the content and so hashed;
  a test changes each in turn and shows the fingerprint moves and the
  save is refused. With the retuned numbers below, saves and traces from
  before this spec do not load. Nothing is migrated.
- **Golden hash.** `kGolden` did not move: it is still
  `0x2a5da3177ab7c69d`. The spec, the design and record 002 all said it
  would move because mine state grew. It did not, because nobody gives an
  order in that run, so no mine is built and the mine list is hashed
  empty. The same was true of `Mine::lowWarnedSurvey` in spec 004. The two
  new fields are pinned by the save and hash tests in
  `tests/mines_test.cpp`. To close the gap a second golden test was added,
  "Golden state hash of a played game is stable across platforms":
  `kGoldenPlayed` = `0xc0980b717725177f`, seed 42 on the in-code fixture
  with the built-in competitor giving both companies' orders for three
  years, so land bought at auction, surveys, mines, loans and research are
  all in the hashed state, and the same value must come back from a save.
  It was recorded on Linux/GCC on 2026-10-10 and has not yet been checked
  on Windows/MSVC. No fixture mine runs at a loss in that game, so it pins
  `lossDays` and `lossAnnounced` at zero, not at a counted value.
  (Both values were superseded by spec 010 step 1; the current ones are
  under "Golden hashes (spec 010, step 1)".)
- **The runner's measures.** `--sweep` adds, per ore, the permille of
  days in years 3 to 20 with price below base-output break-even and the
  lowest and highest yearly mean price over base; per company, mine-days
  idled and cash at the end of year 10; and seeds with a bankruptcy in
  years 1 to 10 and in years 11 to 20, and seeds with an idled mine.
  Mine-days idled counts every mothballed day, whoever chose it and why.
  A seed has an idled mine only when one stood mothballed 30 days or more
  in a row and then went back to operating within the run
  (`idle_reopen_spells` in each seed's row). A mine left mothballed,
  because its extractable ore ran out or for any other reason, shows no
  price decision and does not count. This is narrower than record 003's
  "idled for at least 30 days" and was changed after the Codex review
  (finding 4); record 004 holds the definition.
- **The gates** (record 003) are on the sweep path only, are set for 50
  seeds over 20 years with one rival, and are evaluated only by a sweep
  that covers 20 years; a shorter one prints them as "not evaluated" and
  they pass. Gated: `bite_iron`, `bite_copper`, `bite_nickel` (median
  seed between 100 and 500 permille), `idled_mines` (at least 40 seeds in
  50, rounded up for other counts), `cash_y10` (company 0's median at
  most half the baseline) and `bankruptcy_y1_10` (at most one seed in 50).
  Reported only: `mean_prices` (aim 0.7 to 1.3 of base, lithium 1.0 to
  1.4), `bankruptcy_y11_20`, mine life and withdrawn land. On a 20-year
  sweep the whole-run `seeds_with_bankruptcy` check of spec 006 becomes
  reported only, because the years 1 to 10 gate takes its place. The gates
  of specs 005 and 006 still apply, including a median first operating
  mine under day 250, which is stricter than this spec's 270. A sweep
  exits 3 on a failed gate. `--sanity` is accepted together with `--sweep`
  and adds nothing, so the command the spec gives (AC-009) runs as
  written; before the review round the runner refused the pair.
  (These gates were superseded by spec 010 step 1: the six gates listed
  here are now reported, not gated. See "The gates now" and
  "What is no longer gated" under "The race for the program (spec 010,
  step 1)".)
- **The cash baseline.** `kBaselineCashY10` = 765,667,709: company 0's
  median cash at the end of year 10, in whole currency units, from
  `sim_runner --sweep 50 --years 20 --ai 1` (seeds 1 to 50, release build)
  on 2026-10-10 at commit `e02f48d`, where behaviour still equalled the
  build before this spec. The gate is half of
  it, 382,833,854. Measure it again if data from before spec 008 changes.
- **CTest.** `runner_sweep` is chosen at configure time. A Release,
  RelWithDebInfo or MinSizeRel build without the sanitizers runs the whole
  gated sweep (50 seeds, 20 years) and is judged by its exit code, with a
  180-second timeout and a failure if any check prints "not evaluated".
  It took 28 to 62 seconds in Release depending on the machine's load,
  which is why the design's 60-second ceiling became 180. Any other build
  is too slow for it (a 20-year seed took 0.8 seconds in Release, 5 in
  Debug, 32 under the sanitizers), so it runs two seeds, or one under the
  sanitizers, with a 300-second timeout. So few seeds can fail a median
  by sampling alone, so that run is judged by its output: every check
  must be printed, the four that judge each seed on its own must pass,
  and a sanitizer report fails it. `runner_bite_measures` (two seeds,
  three years) checks that the summary carries the new measures, that
  each seed's row carries its idle-and-reopen spells, and that a short
  sweep reports the bite gates as not evaluated. This replaces the
  ten-seed, eight-year `runner_sweep` of specs 005 and 006. (Superseded by
  spec 010 step 1: in an optimized build `runner_sweep` now runs 50 seeds
over 30 years. See
  "CTest" under "The race for the program (spec 010, step 1)".)
- **The retune is data only.** No AI rule and no sim rule was added to
  pass a gate. The AI's valuation of a prospect now takes the dearest
  quote among the plot's unlocked ores, and its construction reserve is
  `opex_reserve_days` of the dearest ore it can dig, because that is what
  the sim charges.

Every value changed, with the reason and the sweep evidence behind it.
The break-even share is `mine_opex_per_day` /
`base_output_tonnes_per_day` / `base_price`; a data test holds it to 0.5
to 0.7 for iron, copper and nickel and 0.4 to 0.6 for lithium on the
shipped files (the loader accepts any positive cost, so experiments with
`--data` stay possible).

The evidence is from the tuning task's report: about 216 sweeps in 20
batches, each run with `sim_runner --sweep` against a scratch copy of the
data. The table's last column cites these findings by letter.

- **E1.** Running cost at a break-even share of 0.6, with elasticity at 2
  or 3 times its old value and demand unchanged, put bite in range at
  once. It left year-10 cash near 1M, the first mine at day 400 to 1000
  and half the seeds with no depleted mine.
- **E2.** Demand at 2 to 4 times its old value fixed the opening. It
  exposed one-mine companies that never grow and go bankrupt, in 2 to 8 of
  20 seeds.
- **E3.** `opex_reserve_days` 15 and `loan_floor` 5M took stuck companies
  and bankruptcies to about zero. Starting capital, the bootstrap loan,
  the cash reserve and the required return, tried one at a time, did
  less.
- **E4.** Shares of 0.5, 0.6 and 0.7 at varying demand, and a uniform
  elasticity of 0.75 to 1.5 all sat on one line that trades cash against
  depletion. `required_return`, `payback_horizon_days`,
  `max_open_prospects` and `idle_mine_grace_days` did nothing useful.
  `supply_discount` below 1 caused 8 to 15 bankruptcies in 20 seeds.
  None of those five was changed.
- **E5.** `first_mine_recoverable_share` 1.0, starting capital of 10M to
  15M, `research_floor` 1M and `reopen_margin` 1.05 gave the first full
  passes at 20 seeds.
- **E6.** Per-ore sensitivity: iron and copper demand buy depletions and
  fewer bankruptcies; nickel and lithium demand buy only cash. Shifting
  demand that way, with nickel's share at 0.65, gave the first passes at
  50 seeds, with 40M to 70M of margin on the cash gate.
- **E7.** Lithium's mean price over base was 0.85 to 0.95 at elasticity
  1.0, about 0.94 at 1.5 and 1.22 at 2.0. Copper demand of 540, not 500,
  lifted copper's mean from 0.698 to 0.72.
- **Ablation.** Each of the five AI changes and the capital change was
  removed alone from a near-identical base. Each removal failed the
  depletion gate or the bankruptcy gate by 1 to 2 seeds.
  `idle_mine_grace_days` 90 was tried and dropped as unnecessary; it
  stays at 30.

| File | Value | Old | New | Why | Sweep evidence |
| --- | --- | --- | --- | --- | --- |
| resources | iron `elasticity` | 0.5 | 1.5 | a handful of mines must move the price | E1: a steeper response with the new cost put bite in range. E4: 1.5 is the top of the uniform range tried |
| resources | iron `base_demand_tonnes_per_day` | 1600 | 4000 | sized against the steeper response; iron demand buys depletions and fewer bankruptcies | E2, E6 |
| resources | iron `mine_opex_per_day` | 8000 (shared) | 18000 | break-even share 0.22 to 0.50. The AI's valuation charges running cost for 1.3 times the nominal extraction time, so a higher share stops it building small deposits, which are the ones that deplete | E1, E4 |
| resources | copper `elasticity` | 0.7 | 1.5 | as iron | E1, E4 |
| resources | copper `base_demand_tonnes_per_day` | 200 | 540 | as iron | E2, E6. E7: 540 not 500 lifted the mean price from 0.698 to 0.72, inside the 0.7 to 1.3 aim |
| resources | copper `mine_opex_per_day` | 8000 (shared) | 27000 | break-even share 0.15 to 0.50 | E4; no finding specific to copper's share was recorded |
| resources | nickel `elasticity` | 0.8 | 1.5 | as iron. Not 2.4: demand growth of 4% a year raised to that elasticity gave yearly mean prices up to 4.6 times base | E1, E4 |
| resources | nickel `base_demand_tonnes_per_day` | 60 | 90 | raised least of the three early ores, because nickel demand buys only cash | E6. A share of 0.7 at demand 80 put nickel's bite at 455 to 490 permille, too near the ceiling of 500 |
| resources | nickel `mine_opex_per_day` | 8000 (shared) | 28600 | break-even share 0.18 to 0.65; the higher share cut year-10 cash by about 35M | E6 |
| resources | lithium `elasticity` | 1.0 | 2.0 | puts lithium's mean price inside its 1.0 to 1.4 aim; the late ore responds hardest | E7: mean 0.85 to 0.95 at 1.0, about 0.94 at 1.5, 1.22 at 2.0 |
| resources | lithium `base_demand_tonnes_per_day` | 16 | 20 | raised little, because lithium demand buys only cash | E6 |
| resources | lithium `mine_opex_per_day` | 8000 (shared) | 35000 | break-even share 0.11 to 0.50, inside the 0.4 to 0.6 band | E4; no finding specific to lithium's share was recorded |
| ai | `opex_reserve_days` | 60 | 15 | the reserve is days of the dearest ore's cost; at the new costs 60 days stopped a one-mine company affording a second | E3; ablation |
| ai | `loan_floor` | 750000 | 5000000 | lets a one-mine company borrow to expand | E3; ablation |
| ai | `first_mine_recoverable_share` | 0.6 | 1.0 | small opening deposits get built, and they are the ones that deplete by year 8 | E5; ablation |
| ai | `reopen_margin` | 1.25 | 1.05 | output halves over a deposit's life, so mines sat mothballed with a remnant in the ground | E5; ablation |
| ai | `research_floor` | 2500000 | 1000000 | earlier output and cost research keeps ageing mines running and narrows the gap to the leader | E5; ablation |
| balance | `starting_capital` | 5000000 | 12000000 | margin against early bankruptcies and depletion failures (owner decision 2 below) | E5: 10M to 15M was in the first full passes. E3: on its own it did less than the two AI changes. Ablation |
| balance | `market_ratio_min` / `max` | 0.2 / 5 in code | 0.2 / 5 in data | moved, not changed | none needed: a test shows prices identical to the old code |
| balance | `demand_factor_min` / `max` | 0.01 / 1000 in code | 0.01 / 1000 in data | moved, not changed | none needed, as above |
| balance | `mine_loss_notice_days` | none | 5 | new; the spec's starting value | not tuned |

The final sweep, as reported by the tuning task: `sim_runner --sweep 50
--years 20 --ai 1`, release build, seeds 1 to 50, exit 0. "Before" is the
same sweep on the same code with the economy as it stood before tuning.
A blank cell means the figure was not recorded.

| Measure | Before | Gate or aim | After |
| --- | --- | --- | --- |
| Days in years 3 to 20 below break-even, permille, median seed: iron, copper, nickel | 0, 0, 0 | gate: 100 to 500 each | 202, 210, 384 |
| Seeds with a mine mothballed 30 days or more and then reopened | 17 of 50 by the first measure (any 30 mothballed days in a row); no figure under the new one | gate: at least 40 | 50 of 50 by either measure |
| Company 0's cash at the end of year 10, median | 765,667,709 | gate: at most 382,833,854 | 340,963,739 |
| Seeds with a bankruptcy in years 1 to 10 | 0 | gate: at most 1 | 0 |
| Seeds with a bankruptcy in years 11 to 20 | 0 | reported | 0 |
| Median day company 0's first mine operates | | gate: under 250 (spec: 270 or under) | 227 |
| Mines built, leader over trailer, median seed | | gate: at most 3.0x | 1.09x |
| Mean price over base: iron, copper, nickel, lithium | 0.65, 0.56, 0.75, 0.75 | aim: 0.7 to 1.3; lithium 1.0 to 1.4 | 0.71, 0.72, 0.82, 1.22 |
| Mines built per company over 20 years | | | 31 |
| Median withdrawn plots at year 8 | 37 (spec 006) | aim: 25 or fewer | 66 |
| Median life of a mine | | aim: 1,800 to 2,520 days | 2,550 |

- **Idling is real, not parked mines.** Before tuning, the 17 seeds with
  an idled mine were mines left mothballed for thousands of days. After
  it, of 3,163 mines built across the 50 seeds, 1,866 were mothballed at
  some point and 1,862 of those reopened, typically after about 34 days.
  After the Codex review the gate counts only a mine mothballed 30 days
  or more and then reopened. No seed's result changed: 50 of 50 on seeds
  1 to 50 and on seeds 51 to 100, with 68 to 213 such spells per seed.
- **Owner decision 1 (2026-10-10): the tuned set is accepted for the
  playtest although it is fragile to the seed block.** On seeds 51 to 100,
  two gates fail narrowly: 2 seeds have a bankruptcy in years 1 to 10
  against a limit of 1, and 2 seeds have no depleted mine by year 8
  against a limit of 0. The gates that measure bite (price below
  break-even on iron, copper and nickel, idled mines, year-10 cash) pass
  on both blocks. Reason: bankruptcy and depletion will be
  reshaped by mine economics (Earth strategy system 2), so tuning them
  tighter now would be tuning something about to change. CI gates seeds 1
  to 50 only.
- **Owner decision 2 (2026-10-10): starting capital stays at 12M for the
  playtest.** The first mine does not need it: the median first operating
  mine is day 228 at 5M and day 227 at 12M. It is kept because it buys
  margin against the bankruptcy and depletion failures above. It is the
  first thing to judge in play.
- **Not solved, and reported.** Withdrawn land at year 8 rose to 66 plots
  (37 after spec 006; spec 005's aim is 25). Median mine life is 2,550
  days, just over the 1,800 to 2,520 band. Lithium's best year reaches
  about 3.1 times base in the median seed, though its mean is in range.
  The depletion-by-year-8 gate passes with no slack. None of these gate.
- **Open.** Listed in the spec under "Open after build". Among them: the
  losing-mine notice sentence is still the plain draft and is owed its
  polish pass before release; `kGoldenPlayed` has been computed on one
  platform; the tuning was fitted to seeds 1 to 50; the feature has not
  been seen on a display, run through CI on Windows/MSVC or playtested.
  The reviews are done (2026-10-10) and their outcomes are in the spec
  under "Reviews".

## The race for the program (spec 010, step 1)

The spec is `spec/race-for-the-program.md`. Its decision records are
`docs/adrs/F-2026-10-11-first-slice-race-for-the-program-001` to `-004`.
This section covers step 1 only, the race. Plants, mine life and the
final tuning are steps 2 to 4 and are not built. Recorded on 2026-10-11,
after the build. Where the build differs from the design
(`.etc_sdlc/features/active/F-2026-10-11-first-slice-race-for-the-program/design.md`)
the difference is listed under "Departures from the design" below. The
tuned values, the sweep evidence and what is still open are in the spec
under "Decisions" and "Open after step 1".

- **What it adds.** A game now ends. Two companies race to complete a
  three-stage program before a deadline. Each stage needs cash, one
  research project and goods the company has delivered from its own
  stock. The first company to complete the third stage founds the
  program. Before this spec a game ended only when the player went
  bankrupt.
- **The program is content.** `data/program.json` holds `deadline_day`
  (10,800) and `stages`, each with `id`, `cost`, `research` (a tech id)
  and `requires` (a list of `resource` and `tonnes`). It loads into
  `Content::program` (`ProgramDef`: `deadlineDay` and `stages`). The
  loader runs it after resources and techs, because stages name both. It
  requires exactly three stages, a cost of at least one cent, a known
  tech and known resources, at least one good required by every stage,
  and lithium among the third stage's needs (BR-002). A stage's project must be the tech whose
  `requires_program_stage` equals the stage's position counting from
  zero, so the two files cannot disagree. In-code content may leave the
  program empty; the client then shows no race item.
- **Saved state on a company.** `Company::programStage` counts stages
  complete, 0 to 3. `Company::programDelivered` holds tonnes per resource
  delivered toward the stage being worked on, and is cleared when the
  stage completes. `Company::programStageDays` holds the day each stage
  was completed, oldest first, so it has `programStage` entries. On load
  a save is refused when the stage count is above the number of stages,
  when a delivery is negative, above the current stage's need or set for
  a resource that stage does not need, or when the stage days are not
  one per stage complete, in order, at least day 1 and not later than
  today. A company must also hold, among its completed techs, the
  project of every stage it has completed.
- **The world's outcome.** `WorldState::outcome` is `{ ended, day,
  founder, results }`. `results` has one `ProgramResult` per company:
  `None` while the game is being played, then `Founded`, `LesserPartner`
  or `Lost`. The sim sets it once and nothing changes afterwards
  (decision record 001).
- **A save cannot claim an ending the world does not show.** Load
  validation (`validateOutcome` in `sim/src/save.cpp`) checks the
  outcome against the companies and the deadline.
  - A game still being played has an empty outcome, no company with
    every stage complete, and a day at or before the deadline day.
  - An ended game has an outcome day that is not zero and equals the
    state's day.
  - With a founder: the founder is solvent, has every stage complete, is
    the only company that has, completed its last stage on the outcome's
    day, and founded on or before the deadline day.
  - With no founder: the outcome's day is the deadline day and no
    company has every stage complete.
  - Every company's result is the one the rule gives. The rule for a
    company that did not found the program is one function,
    `programResultBesideFounder` in `sim/include/sim/program.h`. The
    ending applies it and the validator checks against it.
- **Two orders.** `DeliverToProgram { company, resource, tonnes }` and
  `CompleteProgramStage { company }` are appended to the `Command`
  variant, so the positions saves write for the older orders do not
  move. Both rules are in `sim/src/program.cpp`.
  - A delivery moves tonnes from stock to the program. It is not a sale:
    it earns nothing and the market does not see it. It cannot be
    undone. It is refused for a bankrupt company
    (`CompanyBankrupt`), an unknown resource, a finished program
    (`ProgramComplete`), a good the current stage does not need or more
    than it still needs (`ProgramNotNeeded`), zero or negative tonnes
    (`InvalidQuantity`) and more than is in stock
    (`InsufficientInventory`). An accepted delivery raises a private
    `ProgramDelivered` notice.
  - Completing a stage needs the stage's research done and every
    requirement delivered in full (`ProgramStageNotReady` otherwise) and
    the cost within the company's available cash (`InsufficientFunds`).
    It charges the cost, clears the deliveries, advances the stage and
    records the day. It raises a public `ProgramStageComplete` notice
    with the cost, the number of the stage just completed and the
    stage's tech.
  - One private function, `completionBlocker`, decides whether a stage
    may be completed. `validateCompleteProgramStage` and
    `programStageReady` both answer from it, so the view's ready flag
    and the order's acceptance cannot differ.
  - `programProgressPermille` is the mean over the stage's requirements
    of the share of each delivered, in thousandths, rounded down. Each
    good counts by its own need. Research and cash are not part of it.
- **Stage research is ordinary research** (decision record 002). The
  three projects are in `data/techs.json` with `requires_program_stage`
  0, 1 and 2 (`TechDef::requiresProgramStage`). A project that waits for
  a stage the company has not completed is refused as
  `TechNeedsProgramStage`. Starting a stage's project raises the usual
  private `ResearchStarted` notice and a public `ProgramResearchStarted`
  notice with the stage's number (BR-004). The projects have no effects.
- **One rule for what can be started.** `sim::researchOpenTo` says
  whether a company may start a project, money and a busy lab aside. It
  and `validateStartResearch` both answer from one private function in
  `sim/src/research.cpp`. A busy lab is still the reason given ahead of
  a missing prerequisite or stage.
- **The ending.** The first company to complete the last stage founds
  the program, inside the commands step, and the game ends with that
  order. The founder is `Founded`. Any other company is `LesserPartner`
  if it has completed at least two stages and is not bankrupt, and
  `Lost` otherwise. A public `ProgramFounded` notice follows the stage's
  own. If the deadline day comes and its orders found nothing, every
  company is `Lost` and a public `DeadlinePassed` notice is raised. A rival's
  bankruptcy does not end the game. The player's own bankruptcy ends it
  for the player in the session, with the outcome still open.
- **A finished world refuses every order.** `Simulation::dispatch`
  checks `gameEnded` for each order, so orders queued behind the one
  that founds the program are refused the same day, each with a
  `CommandRejected` notice and the reason `GameEnded`. A tick on a world
  that has already ended applies the queue, which refuses everything,
  and does nothing else: no step runs and the day does not advance. The
  golden race test ticks a finished world once more and the hash does
  not change.
- **Tick order.** Commands (1), program (1b), plot market (2), surveys
  (3), mines (4), sell policies (4b), markets (5), finance (6), research
  (7), events (8), then the day advances (9). Step 1b is `runProgram`,
  the deadline check: when the game has not ended and the day has
  reached `Content::program.deadlineDay`, it ends the game. Orders are
  applied first, so a last stage completed on the deadline day founds
  the program. Nothing else of the deadline day is played.
- **The day a game ends on is not played out.** The world freezes
  straight after that day's orders (tick step 1, where an order
  completing the last stage founds the program and the orders queued
  behind it are refused) or after the deadline check that follows them
  (step 1b). No later step of that tick runs and the day does not
  advance, so the outcome's day always equals the state's day. Nothing
  that happens later in a day, such as a bankruptcy in the finance step,
  can contradict the results. This was changed in the review fix round;
  as first built the rest of the day ran and the day advanced.
- **Why the program step moved.** Save validation reads the state's day
  as a between-ticks day: every other step leaves its records dated
  before the day the tick ends on. A world frozen after a full pipeline
  would not load. An ended world may hold records from its last day's
  orders, dated today, and validation allows them only when the outcome
  is ended.
- **The view.** Everything about the race is public (BR-007).
  `CompanyView` gained `deadlineDay`, `outcome` (`OutcomeView`),
  `program` (the viewer's own) and `programs` (one `ProgramPublicView`
  per company).
  - `ProgramView` holds stages complete, a complete flag, the stage days
    and the current stage. `ProgramStageView` holds the stage's index,
    cost, tech, research state (not started, in progress, done), the
    project in progress with its finish day, each requirement's needed,
    delivered and in-stock tonnes, the progress in thousandths and
    `ready`, which is `sim::programStageReady`.
  - `ProgramPublicView` holds, for every company, stages complete, the
    complete flag, the stage days, progress in thousandths and the
    research state of its current stage. A rival's entry is as full as
    the viewer's own.
  - `CompanyView::researchOpen` holds one flag per tech, from
    `sim::researchOpenTo`. The AI and the client read it in place of
    their own copies of the rule. A `StartResearch` order for a project
    marked 0 is always refused.
- **The session.** `Session::gameOver()` is true when the player is
  bankrupt or the outcome has ended. `Session::outcome()` returns the
  view's outcome. Once the game is over, `setSpeed` forces the clock to
  paused, `advance` and `stepOneDay` play no day, and `tickOnce` writes
  no history and no trace record. `Session::submit` returns a bool and
  refuses an order once the game is over: nothing is queued or traced
  and the answer is false. The sim would refuse it on the next tick, but
  no tick is coming. Every `GameEnded` rejection reads the same whatever
  the order was: "Couldn't carry out the order: the game is over and no
  more orders are taken." On the day the game ends, the notice
  that ended it (`ProgramFounded`, `DeadlinePassed` or the player's own
  `Bankrupt`) is the one handed over as the reason the clock stopped,
  whatever else the day raised, and the clock stops under any rule set.
  For the Committed figure a `CompleteProgramStage` order costs the
  current stage's cost and a `DeliverToProgram` order costs nothing.
- **The rival-program stop rule.** `game::PauseOn::RivalProgram`, key
  `rival_program`, maps `ProgramResearchStarted` and
  `ProgramStageComplete` from another company to an auto-pause reason
  (BR-010). It is on by default and listed with the other stop rules.
  With the rule off the log line still appears.
- **The trace.** The two new orders are written and read by name. The
  `end` record carries an `outcome` when the game was over for the
  player: `ended`, `day`, `founder` (a company index or null) and
  `result`, one of `founded`, `lesser_partner`, `lost` or `bankrupt`. A
  bankrupt player is `bankrupt` whatever the race came to, and if the
  race is still open the day is the day the session stood on. A game
  closed while still being played writes the plain `end` record. A game
  has one `end` record. It is written at the moment the game ends, when
  a new game replaces it, or on close, whichever comes first; the writer
  ignores later calls until another game starts.
  `kTraceVersion` and `kSaveVersion` are still 1. Saves and traces from
  before this step are refused by the content fingerprint.
- **The AI races** (`sim/src/ai_controller.cpp`, BR-011). It gives no
  orders once it is bankrupt or the game has ended. Its program orders
  are placed ahead of every other rule.
  - It completes a stage on the day the view says the stage is ready.
    When the research and deliveries are in and only the cost is
    missing, the cost is earmarked so no other rule spends it.
  - For a company with standing sell orders, it sets each order's
    reserve to what the stage still needs of that good and releases it
    when the need is met. Sell policies run the day the ore is dug, so
    without this there would be no stock to deliver.
  - It delivers `program_reserve_share` of its stock of each good the
    stage still needs, cut to the need, and sells the rest. The share is
    rounded down, but a company that keeps any share delivers at least
    one tonne when it holds stock and the stage needs it. It delivers
    nothing while its spendable cash is under `program_cash_buffer` plus
    the cost of a program project that is waiting to start.
  - It multiplies the surveyed value of an ore by
    `program_ore_land_weight` when the program is short of it: the
    current and coming stages need more than its operating mines will
    dig before the deadline at the rate they dug yesterday.
  - For research the program comes first. The order is: whatever
    unlocks an ore the current stage needs; the stage's own project,
    once the stage's delivery progress has reached
    `program_research_progress`; whatever unlocks an ore a coming stage
    needs. A program project is paid from spendable cash as long as the
    buffer is left. While it cannot be afforded nothing else is started.
  - The four knobs are in `data/ai.json`. The loader bounds the two
    shares to 0 to 1 and the land weight to 1 to 10.
- **The runner's race measures.** A run stops when its game ends and
  never ticks a finished world. A run of exactly the deadline's length
  makes one tick more, so `--years 30` always reaches an outcome. Days
  in the runner's output count from 1: `founded_day` and `end_day` are
  the outcome's day plus one, like `stage_days`. The ending tick is not
  sampled. A day that never came is printed as
  `never_day`. Each seed's row carries `outcome` (`founded`, `deadline`
  or `running`), `founder` (-1 for nobody), `founded_day`, `end_day`,
  `stage_days` for each company and `rejected_commands`. Orders refused
  because the game had just ended are not counted against the AI. The
  summary carries seeds founded, at the deadline and still running, wins
  by company, the founding day and the founder's day for each stage. A
  figure sampled at a year a seed never reached is null.
- **The gates now.** A sweep's exit code is its gated checks. Gated:
  `opening_has_mineable_plots`, `median_first_mine_day` (under day 250),
  `world_valid`, `ai_commands_valid` and the three race checks of
  BR-013. The race checks are set for 50 seeds and held to the same
  shares, rounded up, for other counts: `founded_before_deadline` (at
  least 40 seeds end by a founding), `win_balance` (no company founds in
  more than 35) and `bankruptcies` (at most one seed). They are
  evaluated only by a sweep that covers the deadline. A shorter one
  prints them as "not evaluated" and they pass.
- **What is no longer gated.** The checks that assumed a 20-year
  open-ended game are still printed, with `"gated": false`:
  `unlisted_land_at_year_8` (spec 005), `seeds_with_bankruptcy`,
  `mines_leader_over_trailer` and `depletion_by_year_8` (spec 006), and
  spec 008's `bite_iron`, `bite_copper`, `bite_nickel`, `idled_mines`,
  `cash_y10` and `bankruptcy_y1_10`. A game now ends when the program is
  founded, so a seed is measured only as far as its game went. Spec
  008's decision record 003 therefore no longer describes the gates in
  force. The record is not edited.
- **CTest.** In an optimized build without the sanitizers,
  `runner_sweep` runs `sim_runner --sweep 50 --years 30 --ai 1 --sanity`
  (AC-010), is judged by its exit code, has a 180-second timeout and
  fails if a gated check prints "not evaluated". The tuning task
  reported 38 to 39 seconds for that sweep on the shipped data. Any
  other build runs two seeds, or one under the sanitizers, for 30 years
  and is judged by its output: every seed's game must have ended and
  every check must be printed. `runner_outcome` (one seed, two years)
  checks that a single run reports a game still running.
  `runner_bite_measures` now also checks that a short sweep reports the
  race checks as not evaluated.
- **The client.** Three additions, all read from the view.
  - The status strip under the header carries a race item on every tab:
    the time left to the deadline in years and days, or who founded the
    program, and a chip that says Ahead, Behind or Level. The Dashboard
    has a race strip with a row per company: stages complete, a progress
    bar with its figure, and the research state.
  - The Program tab is tab 6 by number and second on the rail. It lists
    the three stages, then the current stage's needs (needed, delivered,
    in stock, still needed) with a Deliver button per good that offers
    the lesser of the stock and what is still needed. It shows the
    stage's cost and research, Start research and Complete stage. Orders
    go through the pending queue of spec 007 and can be cancelled.
    Complete stage is live exactly when the view's `ready` is true. The
    tab says what is missing, warns when cover-costs is on, and says
    which project unlocks a good the company cannot dig yet.
  - The end screen covers play once `Session::gameOver()` is true. It
    shows the result, the day the game ended, every company's stages and
    the day of each, the player's cash, mines and stages, and one
    button, New game. The speed buttons are off.
- **Playtest 4 fixes** (BR-014).
  - The losing-mine notice needs a loss worth having. A day is a losing
    day only when the mine's loss is at least
    `mine_loss_notice_min_share` (0.05) of its running cost.
  - The notice is not re-armed by one good day. `Mine::lossClearDays`
    counts consecutive days that were not losing days since the notice.
    The announcement is cleared when the count reaches
    `mine_loss_rearm_days` (30), or when the mine stops counting (it
    stops operating, its owner goes bankrupt or it has no output to
    price). A losing day sets the count back to zero. This replaces the
    spec 008 rule above, under which one profitable day cleared the
    announcement. On load a save is refused when the count is not zero
    on a mine without an announcement, beside a count of losing days, or
    above the re-arm number.
  - The mothball notice says what happened. `MineMothballed` and
    `MineReopened` are two private notice kinds, each with its own
    sentence.
  - The Market tab shows prices exact to the cent (`game::exactPrice`,
    in `game/session/include/game/format.h`).

### Golden hashes (spec 010, step 1)

All three values are in `tests/determinism_test.cpp`, use the in-code
content fixture and were recorded on Linux/GCC. They are confirmed for
Windows/MSVC only when CI runs.

- **`kGolden`** (seed 42, one year, no orders) moved twice.
  - `0x2a5da3177ab7c69d` to `0x01906a0707315707`: every company gained
    its program stage and deliveries, and the world gained an outcome.
  - `0x01906a0707315707` to `0x059a7c79213dd307`: every company gained
    its list of stage days. Nobody completes a stage in this run, so the
    list is hashed empty, by its length alone.
  - `Mine::lossClearDays` did not move it, for the reason spec 008's
    mine fields did not: no mine is built in this run.
- **`kGoldenPlayed`** (seed 42, three years, the AI gives both
  companies' orders) moved four times.
  - `0xc0980b717725177f` to `0xb6d511501247ec2d`: the program stage and
    deliveries on a company and the outcome on the world. No rule
    changed.
  - `0xb6d511501247ec2d` to `0x59fab58dbf9f668d`: every mine gained
    `lossClearDays`, the losing-notice re-arm count. With the field left
    out of the saved form the old value still held, so the game played
    was the same.
  - `0x59fab58dbf9f668d` to `0x1d4743bc9b26a84f`: the AI now races. It
    delivers, sets sell-order reserves, researches toward the program
    and completes stages, so the game it plays is a different one. No
    saved field changed, and `kGolden` did not move.
  - `0x1d4743bc9b26a84f` to `0x41192679894c4afc`: the stage days on a
    company. With the field left out of the saved form the old value
    still held. The same change added `researchOpen` to the view, which
    the AI reads; that did not move the value.
- **`kGoldenRace`** = `0xae14b05ab3e56760` is new: "Golden state hash of
  a played race is stable across platforms". It was first recorded as
  `0x588363dbb9c693f3` and moved once, in the review fix round, when the
  world began to freeze on the day it ends: the rest of that day's
  pipeline no longer runs and the day does not advance. `kGolden` and
  `kGoldenPlayed` did not move for that change, since neither game ends. Neither run above is
  certain to reach the program's rules. Here both companies are given
  stock and cash and race by rote on the fixture's small program, with
  real orders applied by ticks. The player completes all three stages
  and founds the program. The rival starts on day 45, completes two and
  is part-way through the third. The state hashed holds stage days,
  deliveries toward an unfinished stage, a stage project still running
  and an outcome with a founder and a lesser partner. The same value
  must come back from a save, and one more tick must not change it.
- **Once per step.** The spec asks for the golden hashes to move once
  per step (BR-026, AC-019). They moved once per change to state or
  behaviour instead, as the tasks landed, with each reason above.

### Departures from the design (spec 010, step 1)

- **`deadline_day` is in `data/program.json`**, not `data/balance.json`.
  The deadline belongs to the race and loads with it.
- **The lesser-partner threshold is a named constant**,
  `kLesserPartnerStages = 2` in `sim/src/program.cpp`, not data. It is
  part of the rule: the spec fixes it at two of the three stages.
- **The mine field is `lossClearDays`**, not `profitDays`. It counts
  days that are not losing days, which includes a day at break-even and
  a day with a loss under the set share.
- **`MineSwitched` became two notice kinds**, `MineMothballed` and
  `MineReopened`. The design did not say how the notice would be fixed.
  BR-014 asks for it to say which happened.
- **A rejected order's notice says which kind of order it was.**
  `Notice::order` is a `RejectedOrder` (`Other`, `Delivery`,
  `StageCompletion`), set only on `CommandRejected`. A refused delivery
  carries a resource and tonnes exactly as a refused sale does, and a
  refused stage completion carries nothing, so without it the log could
  not word either.
- **`programStageReady` and `programProgressPermille` take the world
  state** and a company id. The design gave them the content and a
  company. Readiness needs available cash, which is read from the state.
- **The deadline is in the status strip under the header**, not in the
  header row. It did not fit there once the company name and the figures
  were long ones.
- **Stage days were added to saved state and the view.** The design
  kept only the count of stages complete. The end screen needed the days
  in a long game and after a load, when the session's log is gone, so
  the sim keeps them (`Company::programStageDays`). The runner reads its
  own from the `ProgramStageComplete` notices.
- **A fourth AI knob**, `program_research_progress`. The design listed
  three.
- **The runner gates only the opening, validity and race checks.** The
  design added the race checks and did not say the older gates would
  stop gating. Spec 008's bite checks are reported and no longer gated,
  so spec 008's decision record 003 no longer describes the gates in
  force.
- **Excess is refused, not cut.** The design's layer table says a
  delivery "is capped at the remaining need". The sim refuses a delivery
  above the remaining need as `ProgramNotNeeded`, as AC-002 asks. The
  cut to the need is made by whoever gives the order: the AI and the
  Program tab's Deliver button.
- **The runner's own default is still 20 years.** The design said the
  gated sweep's default becomes 30. The `runner_sweep` CTest passes
  `--years 30` instead.
- **The same-day finish is not recorded.** The spec's first edge case
  says two companies completing stage 3 on the same day is "recorded in
  the outcome as a same-day finish". `Outcome` has no field for it. The
  lower company index is applied first and founds the program. The
  other's order is refused as `GameEnded` and it finishes as a lesser
  partner on its two stages. The Codex review raised this and it was
  declined: the first order applied wins.
- **The program step follows the orders, not the events.** The design's
  tick order put the program step after events, and as first built the
  day a game ended on was played out. Now the deadline check is step 1b
  and the world freezes on the day it ends. The reason is that save
  validation reads the state's day as a between-ticks day, so a world
  frozen after a full pipeline would not load. It also means a
  bankruptcy later the same day cannot contradict a lesser-partner
  result.

### Numbers and what is open (spec 010, step 1)

The shipped numbers are provisional. Base demand was raised to 3 to 10
times spec 008's, by ore, so one standard mine moves its own price by
about 4.8 percent (BR-012; a data test limits it to 6 percent). The
program's costs and tonnages and the stage projects' costs and durations
were retuned against it. Every value, its reason and the sweep evidence
are in the spec under "Tuned and provisional values", with the final
sweeps on seeds 1 to 50 and 51 to 100. What is not done is in the spec
under "Open after step 1". Among it: about 30 sentences are unpolished
drafts; the race is paced by the research clock and the deadline hardly
bites; spec 008's bite is largely gone at this market depth; the golden
values are unconfirmed on Windows; the client has not been seen on a
display; and the security review and the spec-compliance check have not
been run for step 1. The code review and the Codex review are done
(2026-10-11) and their outcomes are in the spec under "Reviews".

## Reading the race (spec 011)

The spec is `spec/reading-the-race.md`. Its decision records are
`docs/adrs/F-2026-10-18-reading-the-race-001` to `-004`. Recorded on
2026-10-19, after the build on branch `spec-011-reading-the-race`, which
is not merged. Where the build differs from the design
(`.etc_sdlc/features/active/F-2026-10-18-reading-the-race/design.md`)
or the spec, the difference is listed in the spec under "Build
(2026-10-19)", with the commit. Four of them wait for the owner.

- **What it adds.** Playtest 5 (`docs/playtests/2026-10-18.md`) lost
  the race without the player seeing it. This spec makes the race
  readable without changing its pace. Each company has an earliest
  founding day, worked out by the sim; the verdict, the status strip,
  the race strip and the Program tab lead with it. Falling behind is a
  notice and a clock stop. The Program tab says where each ore the
  program needs comes from, and the Plots tab marks land that may hold a
  missing one. A clock stop keeps its reason on screen until the clock
  starts again. No balance number changed (BR-015).
- **The earliest founding day** (`sim::earliestFoundingDay` in
  `sim/include/sim/program.h`, decision record 001). The earliest day a
  company could found the program were every remaining stage project
  started as soon as the rules allow and goods and cash never held it
  up. It counts from `WorldState::day`, the day an order given now
  applies.
  - A project started on day D finishes on D plus its duration. Its
    stage's completion order applies the day after, and the next
    stage's project can start that same day.
  - A lab busy with another project takes the stage's project the day
    after that project ends.
  - `foundingDayIfStartedOn(content, stage, startDay)` is that
    arithmetic: the start day plus, for the stage and each later one,
    the project's duration and one day. On the shipped data, three
    stages not started give today plus 4,683.
  - A founded program's day is its last stage day. The day is empty for
    a bankrupt company or content with no program.
  - It reads research records, stages and their days, and bankruptcy,
    never cash or deliveries. No research can be sped up, so the day
    never moves earlier.
  - `leadingRivalFoundingDay` is the earliest day among the company's
    solvent rivals; on a tie the lowest company index leads.
  - Two tests have one statement each, and every caller answers from
    it. `behindLeadingRival`: the company's day is later than its
    leading solvent rival's. `foundsPastDeadline`: the company's day is
    after the deadline day; a founding on the deadline day is in time.
  - A rival's busy lab is counted, so the day its non-program project
    ends is public (owner, GA-008). The privacy comment in
    `sim/src/view.cpp` says so.
- **Content.** Two keys, both required and both in the content
  fingerprint, so saves and traces from earlier builds are refused.
  - `program_pace_warning_days` in `data/program.json`, 360, loads into
    `ProgramDef::paceWarningDays`. The loader takes an integer from 0
    and refuses one above `deadline_day`.
  - `storage_warning_cash_share` in `data/balance.json`, 0.1, loads into
    `BalanceParams::storageWarningCashShare`. The loader takes a decimal
    above 0 and up to 1.
  - The loader refuses a stage project that requires other research
    (owner, GA-009), so the earliest founding day counts a stage's own
    time and nothing before it.
- **Two notices.** `ProgramFellBehind` and `ProgramPastDeadline` are
  appended to `NoticeKind`, both private to their company. `Notice`
  gained `rival` and the two days `foundingDay` and `rivalFoundingDay`,
  as `std::optional<Day>` because day 0 is a real day. Every older kind
  leaves them empty.
- **Tick step 9b, the pace check.** `runProgramPace` runs after the day
  advances, the one step that does, so it compares the earliest founding
  days the next view shows. While the game is open, for each solvent
  company in index order, it sets `Company::programBehind` to whether the
  company's earliest founding day is later than its leading solvent
  rival's, and `Company::programPastDeadline` to whether that day is after
  the deadline. A change from 0 to 1 raises the private
  `ProgramFellBehind` (naming the lowest-indexed rival on the leading day,
  with both days) or `ProgramPastDeadline`; drawing level or ahead clears
  `programBehind` with no notice. A bankrupt company's flags are left as
  they stood. Both flags are saved and hashed. See
  `docs/adrs/F-2026-10-18-reading-the-race-002-pace-check-tick-step-and-flags.md`.
  - Both flags are recomputed every tick. The design made
    `programPastDeadline` stick once set; recomputing it gives the same
    answer, because the earliest founding day never moves earlier.
  - The step is the last in the tick and changes only its own flags.
    The pipeline comment in `sim/src/simulation.cpp` and "Determinism
    rules" above name it.
- **A save cannot claim a pace the world does not show.**
  `validatePace` in `sim/src/save.cpp` runs last in load validation,
  after the checks that bound the research records and stage days it
  reads. In a game still being played, each solvent company's flags
  must equal `behindLeadingRival` and `foundsPastDeadline` on the saved
  state. A bankrupt company and an ended game are not checked.
- **The view carries every figure and decision** (R-001). The client
  formats them and works out no part of the race.
  - `ProgramPublicView`, for every company: `earliestFoundingDay`,
    `researchDueDay` (the stage project's finish day while it is under
    way) and `pastDeadline` (`foundsPastDeadline`).
  - `ProgramView`, the viewer's own, while its stage project has not
    started and the game is open: `lastStartDay`, the last day to start
    it and still found no later than the target. The target is the
    leading solvent rival's earliest day or the deadline day, whichever
    is earlier; with no solvent rival it is the deadline. It is worked
    out in signed days, since against a rival close to founding it can
    fall before day 0. When it is before today, `lastStartPassed` is set
    and `lastStartDay` is empty. `labFreeDay` is the day after another
    project ends. `paceWarning` is set once the day has passed, or from
    `program_pace_warning_days` before it.
  - `ProgramView::missingOres`, one `MissingOre` per resource: `None`,
    `Later` or `Soon`. An ore is missing when what the stages not yet
    complete still need of it (the current stage's need less its
    deliveries, plus every later stage's whole need) is more than stock,
    and none of the company's mines yields it. `Soon` means the current
    or the next stage needs it. A mine yields an ore when it is
    building, operating or mothballed and its extracted or best-estimate
    tonnes of that ore are above zero. The rule is public,
    `sim::mineYieldsOre` in `sim/include/sim/view.h`, so the Plots tab
    reads it and does not copy it. A locked ore (lithium) counts like
    any other.
  - `CompanyView::storageWarning`: the monthly storage cost is above
    zero and either cash is at or below zero or the cost is above
    `storage_warning_cash_share` of cash. The threshold was `cash / 10`
    in `game/src/ui_model.cpp`; this clears that do-not-copy item.
- **Cover costs sells free stock first** (BR-011, decision record 003).
  `coverCosts` in `sim/src/finance.cpp` sells in two passes over the
  same goods, those unlocked and in stock: first the free stock, of each
  good the stock above what the current stage still needs (its
  requirement less its deliveries); then the stock the stage still
  needs. Each pass is ordered by the value of its own tonnes, most valuable
  first, with ties to the lower resource id. Each sale is capped by
  `saleTonnesWithinBounds` and by the tier's tonnes, and stops once
  cash reaches zero. A good sold in both passes gets a sale notice for
  each. With no program need the first pass is all the stock, so the
  order is the old one. It is an order, not a reserve: a company short
  of cash with only program stock still sells it, last.
- **The session keeps the day's stops** (decision record 004).
  `Session::stopsToday()` replaces `takePausedBy()`, which handed over
  one notice once. It returns the day's notices that matched an enabled
  Stops rule, in log order. It is cleared when the clock leaves Paused,
  on load, and at the start of each tick, so it always holds the day's
  own stops. On the day the game ends, the notice that ended it comes
  first. The spec 010 section above describes the older hand-over.
  - `LogEntry::stoppedClock` marks each line that stopped the clock.
    The line of the notice that ended the game is marked too, though it
    matches no rule, since the end stops the clock under any rule set.
  - `PauseOn::FellBehind`, key `fell_behind`, is appended and on by
    default. It covers both new notices, for the player only. A settings
    file without the key keeps the default.
  - `isRaceStop` is true for `RivalProgram` and `FellBehind`.
  - None of this is saved. A loaded game shows no stop; its clock is
    stopped anyway.
- **The client.** All read from the view and the session.
  - The verdict: "cannot found the program in time" when the view's
    `pastDeadline` says so; otherwise the gap in days to the leading
    rival's earliest founding day, behind or in hand, and Level on the
    same day. Stages and share delivered decide only when a day is
    missing.
  - The status strip names the leading rival and the time before it
    could found the program ("in 6 y 120 d") when that comes before the
    deadline. The race strip gives each company's earliest founding day
    and its research's due day. Other dates use the existing formatter
    ("Y17 M5 D3").
  - The Program tab gives the last day to start the stage's research,
    or says it has passed, and names a busy lab with the day it is
    free. It says where each needed ore comes from: regions and odds,
    the research that unlocks it, the player's mines that yield it, and
    plots whose surveys show it.
  - Needs you gains a pace row and a row for each `Soon` missing ore.
    The Plots tab marks a listed plot with "may hold" or, where the
    player's survey shows the ore, "survey shows". The storage row reads
    `storageWarning`.
  - The stop panel, from `stopsToday()`, replaces the "Paused: …" status
    line and its real-time timer. It gives the rule as the Stops menu
    names it, the first stop's line, "+N more", a button to the tab
    where the player acts, and a button that switches the rule off. A
    race stop has its own heading and both earliest founding days.
  - The end screen counts open mines, leaving out worked-out and closed
    ones. A standing bid shows in the plot panel and the plot's row.

### Golden hashes (spec 011)

All three values are in `tests/determinism_test.cpp`, use the in-code
content fixture and were recorded on Linux/GCC on 2026-10-19. They are
confirmed for Windows/MSVC only when CI runs.

- **The pace flags and step 9b** moved all three once. Every company
  gained `programBehind` and `programPastDeadline`, set by the new step.
  No other rule changed, and the AI reads only the view, which the step
  does not touch, so each run plays the same game; the checks on what each
  run leaves behind still pass.
  - **`kGolden`** (seed 42, one year, no orders): `0x059a7c79213dd307`
    to `0xc7cc092323bf84f7`. Nobody gives an order, so both companies
    stay level and far from the deadline, and the flags are hashed as
    zero.
  - **`kGoldenPlayed`** (seed 42, three years, the AI gives both
    companies' orders): `0x41192679894c4afc` to `0x43349088476870b5`.
    The rival ends the run behind, so its `programBehind` is hashed as
    one.
  - **`kGoldenRace`** (the race by rote on the fixture's program):
    `0xae14b05ab3e56760` to `0xf576ea4384386ae7`. The rival starts 45
    days after the player, falls behind on the first day and never draws
    level. The test now checks both companies' `programBehind`.
- **Cover costs sells free stock first (BR-011)** moved none of them.
  The new order changes behaviour only where the month-end cover-costs
  sale runs, and it runs in none of the three: the fixture leaves
  `defaultCoverCosts` off, the competitor never gives `SetCoverCosts`,
  and the race world turns it on for neither company. The rule reaches
  the sim runner's company 0, whose cover costs are on, so its effect on
  balance is in the gated sweep's before and after figures, not here.
  It is pinned by the cover-costs tests in `tests/finance_test.cpp`.
- **Once, not twice.** The spec (BR-016) and the design expected a
  second move for cover costs. It did not come, for the reason above.
  The spec lists this under "Build (2026-10-19)".

### Gated sweep (spec 011)

Release preset, `sim_runner --sweep 50 --years 30 --ai 1 --sanity`,
run before the spec at `786e9bd` and after it at `6de2775`.

| Seeds | Gated checks, before and after | Seconds before | Seconds after |
| --- | --- | --- | --- |
| 1 to 50 | all passed | 37.51 | 37.47 |
| 51 to 100 | all passed | 42.02 | 38.13 |

The JSON output of both sweeps is byte-identical before and after. No
figure moved, so the two-tier cover costs changed no sale in the
runner's 100 games, and BR-015 (no balance change) holds. The running
time is within the design's 5% budget.

### Departures and what is open (spec 011)

The full list, with commits, is in the spec under "Build (2026-10-19)".
Those that change what this section describes:

- **One golden hash move**, not two (above).
- **The day's stops also clear at the start of each tick**, and the
  game-ending line is marked though it matches no rule (above).
- **`programPastDeadline` is recomputed**, not sticky (above).
- **The notice's days are `std::optional<Day>`**, not `Day`.
- **`sim::mineYieldsOre` is public**, so the client does not copy the
  rule.

Four departures wait for the owner: Level on equal days even when
deliveries differ (BR-002 against edge case 5); dates as "Y17 M5 D3";
the pace warning showing from day 0, while neither company has started
its stage research; and a player who starts other research while the
rival's lab is free falling behind that day.

Still open: the monitored playtest 6 (AC-012) and the Codex review;
the golden values on Windows/MSVC; `kLesserPartnerStages` in code
(GA-010); the unused `status.paused_by` string; the `.meta`
descriptions, which still name `takePausedBy` and lack `validatePace`
and `mineYieldsOre`. Eight clang-tidy findings the spec added to
`game/src/layout_probe.cpp`, outside CI's tidy gate, were fixed by
remediation 018 (`9a1b04d`).
