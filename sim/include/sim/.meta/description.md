# sim (public headers)

**Purpose:** The 29 public headers of the `sim` library, all in `namespace sim`. They declare the value types, static content structs, command and notice vocabulary, the complete dynamic `WorldState`, per-company views, each rule system's API, the serialization visitor, and the `Simulation` facade.

## Key Components

Foundation types
- `types.h` — `Day`, `Seed`, calendar constants (30-day months, 12-month years), `kMaxDay`, `kMaxDeadline`, and quantity bounds (`kMaxDepositTonnes`, `kMaxStockTonnes`, `kMaxCashCents`, `kMaxSupplyPerDay`).
- `ids.h` — `StrongId<Tag>` (uint32, `kInvalid` = 0xFFFFFFFF) and the aliases `CompanyId`, `RegionId`, `PlotId`, `MineId`, `ResourceId`, `TechId`, `LoanId`, `SurveyId`, `EventId`, `AuctionId`, `ResearchId`, `ActiveEventId`.
- `fixed.h` — `Fixed` (decimal fixed point, `kScale` = 1,000,000) and the checked int64 helpers `mulDiv`, `checkedMulDiv`, `compareProducts`, `checkedAdd`, `checkedSub`, `checkedNegate`.
- `money.h` — `Money` (integer cents), `scaled(Fixed)`, `ratioTo(Money)`, `toString`.
- `math.h` — integer-only `clampFixed`, `fixedLn`, `fixedExp`, `fixedPow`.
- `rng.h` — `Pcg32` generator (`next`, unbiased `below`, `state`/`increment`/`restore`).
- `random.h` — `RngStreams` (five `Pcg32`: `world`, `market`, `survey`, `event`, `ai`), `uniformFixed`, `uniformInt`, `approxNormal`.
- `hash.h` — `Fnv1a64`, header-only FNV-1a 64-bit over little-endian bytes.
- `fatal.h` — `[[noreturn]] fatal(const char*)`.

Data model
- `content.h` — static content structs: `ResourceDef`, `TechDef`/`TechEffect`/`TechEffectType`, `RegionDef`/`DepositPrior`, `EventDef`/`EventEffect`/`EventEffectType`, `BalanceParams`, `AiParams`, and `Content` with `findResource`/`findTech`/`findRegion`/`findEvent`.
- `sell_policy.h` — `SellPolicy` (enabled, reserve tonnes, floor price), shared by `BalanceParams` and `Company`.
- `world_state.h` — `WorldState` and its entities: `Plot`, `PlotListing`, `DepositTruth`, `PlotDeposits`, `HiddenState`, `Company`, `CompanyKind`, `Market`, `Loan`, `Bid`, `Auction`, `SurveyEstimate`, `Survey`, `Mine`, `MineState`, `Research`, `ActiveEvent`.
- `commands.h` — the ten command structs and `Command` variant (`BidOnPlot`, `CommissionSurvey`, `BuildMine`, `SetMineState`, `SellOre`, `TakeLoan`, `RepayLoan`, `StartResearch`, `SetSellPolicy`, `SetCoverCosts`), `RejectReason`, `NoticeKind`, `Notice`, `TickReport`, name and visibility helpers.
- `view.h` — `CompanyView` and its parts (`PlotView`, `AuctionView`, `SaleView`, `SurveyView`, `MineView`, `MarketView`, `LoanView`, `ResearchView`, `ActiveEventView`, `CompanyPublicView`), `makeCompanyView`, constants `kRecentSales` (16), `kRecentPriceDays` (90), `kMajorDepositMult` (2).

Rule systems (tick pipeline step in parentheses)
- `plot_market.h` — (2) `listPlots`, `runPlotMarket`, `reservedCash`, `availableCash`, `validateBid`, `applyBid`.
- `surveys.h` — (3) `surveyNoise`, `validateSurvey`, `applySurvey`, `runSurveys`.
- `mines.h` — (4) `mineCapex`, `mineCapexQuote`, `mineDailyOpex`, `mineDailyOutput`, build and mothball validate/apply, `runMines`, `mineHasExtractable`, `warnIdleMines`, `latestOwnSurvey`, `estimatedRemainingTonnes`, `warnRunningLow`, `kRunningLowDays`.
- `markets.h` — (4b, 5) `initMarkets`, `validateSell`/`applySell`, `executeSale`, `saleTonnesWithinBounds`, sell-policy and cover-costs validate/apply, `recordAutoSale`, `runSellPolicies`, `runMarkets`.
- `finance.h` — (6) `debitCash`, `outstandingPrincipal`, `activeLoanCount`, `monthlyStorageCost`, loan validate/apply, `runFinance`.
- `research.h` — (7) `techCompleted`, `prerequisitesMet`, `activeResearch`, `validateStartResearch`, `applyStartResearch`, `runResearch`.
- `events.h` — (8) `eventMultiplier`, `runEvents`.
- `tech.h` — `techMultiplier`, `resourceUnlocked` (read by mines, surveys, finance, views).
- `worldgen.h` — `generateWorld`, `RegionStart`, `classifyRegionAtStart`.

Orchestration and persistence
- `simulation.h` — `GameSetup` and class `Simulation` (`submit`, `collectCommands`, `tick`, `reportFor`, `view`, `priceHistory`, `stateHash`, `save`, `load`).
- `controller.h` — abstract `Controller` with `decide(const CompanyView&, std::vector<Command>&)`.
- `ai_controller.h` — `HeuristicController`, plus `aiValuedPrice` and `aiBidJitterPercent` exposed for tests and tooling.
- `archive.h` — header-only CRTP `ArchiveBase` with `HashArchive`, `WriteArchive`, `ReadArchive`, and `hashOf`, `toBytes`, `fromBytes`.
- `serialize.h` — header-only `serialize(Archive&, T&)` overloads for every core, world-state, command and content type.
- `save.h` — `kSaveVersion`, `hashWorld`, `hashContent`, `validateWorld`, `saveGame`, `loadGame`.

## Dependencies
- Headers include only each other (`"sim/..."`) and the C++ standard library (`<cstdint>`, `<vector>`, `<string>`, `<string_view>`, `<map>`, `<optional>`, `<variant>`, `<span>`, `<compare>`, `<type_traits>`, `<new>`, `<algorithm>`, `<cstddef>`).
- No includes of `content/`, `platform/`, `game/` or any third-party library.
- Layering inside the directory: `types.h`, `ids.h`, `fixed.h`, `hash.h`, `rng.h`, `fatal.h` include nothing from `sim/`; `money.h`, `math.h` build on `fixed.h`; `content.h` and `world_state.h` build on those; rule-system headers include `commands.h`, `content.h`, `world_state.h`; `simulation.h` sits on top.
- Included by: `sim/src/*.cpp`, `content/` (loader fills `Content`), `game/session/`, `game/src/`, `sim_runner/`, `tests/`.

## Patterns
- Strongly typed ids: `StrongId<Tag>` is a typed index into the owning vector in `WorldState`.
- Value types with checked arithmetic: every `Fixed` and `Money` operator funnels through `mulDiv`/`checkedAdd`/`checkedSub`/`checkedNegate`.
- Command pattern: `Command` is a `std::variant`; each rule header exposes `validateX` returning `std::optional<RejectReason>` and `applyX` mutating `WorldState`.
- Visitor serialization: one `template <class Archive> void serialize(Archive&, T&)` per type is reused by hashing, writing and reading (CRTP `ArchiveBase<Derived>`).
- Strategy: `Controller` is the single interface for the player's controller (in the game client) and `HeuristicController` (in this library).
- Plain aggregate structs for content, state and views; booleans in persisted state are `std::uint8_t`.

## Constraints
- `Fixed` and `Money` truncate toward zero; `Fixed::fromDouble` is for the content-loading boundary only and must not be called from sim logic (`fixed.h`).
- `Pcg32` is implemented locally because `<random>` distributions differ between libstdc++ and MSVC (`rng.h`); `Fnv1a64` must be used for state hashes, never `std::hash` (`hash.h`).
- One RNG stream per system so that adding a draw to one system does not perturb the others (`random.h`).
- Ids are never reused and entities never erased from their vectors (`ids.h`, `world_state.h`).
- `WorldState` holds everything that affects the future of the simulation; `TickReport` is transient and not part of it.
- `HiddenState` must not be read by view code (`world_state.h`, `view.h`); hosts must show a company only `Simulation::reportFor(company)`, not the unfiltered `tick()` report.
- `Content` must outlive the `Simulation` and never change while in use; `Content::strings` and the `description` prose fields are not part of the content hash.
- `AiParams` is hashed into the content fingerprint, so changing an AI knob invalidates saves and traces (`content.h`).
- Serialization encoding (`archive.h`): primitives little-endian at natural width, bools one byte, enums at underlying width, vectors and strings with a uint32 length prefix; reading never throws and reports failure through `failed()`.
- Save layout (`save.h`): magic `DCSV`, uint32 version, uint64 content fingerprint, `WorldState`, queued commands; version stays 1 and saves are not compatible across commits until the first public build.
- `fixedExp` accepts only [-29, 29]; `fixedLn` and `fixedPow` require positive arguments (`math.h`).
- `Simulation::view` aborts on an invalid company id; `Simulation::load` returns false and leaves state untouched on an invalid save.
