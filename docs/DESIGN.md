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
  submission order) -> resolve auctions -> advance surveys -> mines extract ->
  update markets -> month-boundary finance and bankruptcy -> research ->
  events -> advance day and emit tick report.

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
  error flag.
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
  target; price is clamped to [floor, cap] multiples of base.
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
  scales every mine's daily opex (`mineDailyOpex` now takes the state), and
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
  Its knobs live in code because they shape one opponent, not the economy.
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
  [0.01, 1000] before it meets base demand.
- Validation: pending research must have been startable (prerequisites
  complete, tech unknown), projects of one company never overlap, event
  instances of one definition never overlap and respect `min_year`, start
  days are in the past, deadlines are computed in 64 bits.
- Views gained `mineCapexQuote` per plot, `mineOpexQuote` and
  `surveyNoiseQuote` per company, and `lastDayTonnes` per own mine, so the
  UI can show costs and today's output without re-deriving rules. The active
  research view no longer carries its global index, which leaked how many
  projects other companies had started.

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
  nickel 36k, lithium 62k) and mothballed mines pay 15%. Iron stays the
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
  spendable cash next to cash, finance shows loan terms and insolvency
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
  and may be overwritten), `command` (the player's order, tagged with the
  day it was submitted), `day` (day and state hash after every tick),
  `notice` (each player-visible log entry with its text), `state` (monthly
  summary: cash, reserved, debt, capacity, plots, mines with output, stock,
  prices, rivals), `ui` (tab, select_plot, select_resource, speed, status),
  `marker`, `screenshot` and `end`. Seeds travel as decimal strings and
  hashes as hex strings so JSON tools that read numbers as doubles do not
  corrupt them. The trace is written by the session (orders, ticks,
  notices, monthly state) and the app (new games, loads, saves, interface
  events); the sim knows nothing about it.
- **Where**: the `DEEP_CLAIM_PLAYTEST` environment variable if set (`off`
  disables tracing); otherwise `playtest/` in the repository for a
  development build (gitignored); otherwise `playtest/` under the per-user
  data directory. The build id is the short git revision captured at
  configure time.
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
