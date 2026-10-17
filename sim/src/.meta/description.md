# src

**Purpose:** Implementation of the `sim` static library: the daily tick pipeline, each rule system's validation and state mutation (including the race for the program and the ending), the heuristic AI competitor, per-company view construction, world generation, save validation, and the deterministic arithmetic and RNG underneath them.

## Key Components

Orchestration
- `simulation.cpp` — `Simulation`: constructor (seed RNG streams, `generateWorld`, `initMarkets`, create player and AI companies, size `outcome.results` to them, list opening plots); `submit`/`collectCommands`; `applyCommands` (stable sort by company id, `dispatch` rejecting every order as `GameEnded` once the game has ended, otherwise `std::visit` to `handle` overloads that call `validateX` then `applyX` or emit `CommandRejected`, with `RejectedOrder` set for refused deliveries and stage completions); `tick()` running the fixed pipeline and stopping the world when the game ends; `reportFor`, `view`, `stateHash`, `save`, `load`.
- `commands.cpp` — `commandCompany`/`setCommandCompany`, `rejectReasonName`, `noticeKindName` (stable string identifiers), `noticeIsPublic`, `noticeVisibleTo`.

Rule systems (pipeline order)
- `program.cpp` — steps 1 and 1b: `DeliverToProgram` (stock to the program, up to what the current stage still needs; not a sale) and `CompleteProgramStage` (research done, deliveries met, cost within available cash; charges the cost, records the day, moves to the next stage); completing the last stage calls `endGame`, which sets `WorldState::outcome` (founder `Founded`, others `LesserPartner` with at least `kLesserPartnerStages` = 2 stages and not bankrupt, else `Lost`) and raises `ProgramFounded`; `runProgram` ends the game with `DeadlinePassed` on `program.deadlineDay`; `programStageReady`, `programProgressPermille`, `gameEnded`.
- `plot_market.cpp` — step 2: `openAuction`, `closeAuction` (highest affordable bid wins; unsold plots relist at a lower ask or are withdrawn after `balance.maxUnsoldRounds`), `listPlots`, `runPlotMarket` (monthly listings), `reservedCash`/`availableCash`, `validateBid`/`applyBid`.
- `surveys.cpp` — step 3: survey commissioning and `runSurveys`, which samples log-normal estimates from hidden deposits using `state.rng.survey`.
- `mines.cpp` — step 4: capex and output formulas; per-ore running cost (`mineRunningCost`: the highest `mineOpexPerDay` among the ores the mine last produced, or the owner's unlocked ores when none), `mineOpexQuote`, `mineDailyMargin`, `mineBreakEvenPrice`; build and mothball commands (`MineMothballed`/`MineReopened` notices); `runMines` (finish construction, extract, charge running cost, mark depletion, then `updateMineLosses`: count consecutive losing days at yesterday's prices and raise `MineLosingMoney` once per episode, re-armed after `balance.mineLossRearmDays` non-losing days); month-end `warnIdleMines` and `warnRunningLow`, `estimatedRemainingTonnes`.
- `markets.cpp` — steps 4b and 5: manual sales, `runSellPolicies` (standing orders), `runMarkets` (mean-reverting demand walk on `state.rng.market`, smoothed supply, power-law target price via `fixedPow`, partial daily adjustment, floor/cap clamp, price history). The demand-factor and demand/supply-ratio clamps come from `balance.demandFactorMin/Max` and `balance.marketRatioMin/Max`.
- `finance.cpp` — step 6, month boundaries only: `chargeStorage`, `payLoans`, `coverCosts`, `summarizeAutoSales`, `checkSolvency`/`declareBankrupt`; loan commands; `debitCash`, `monthlyStorageCost`.
- `research.cpp` — step 7: start-research command and `runResearch` completing due projects. `researchOpenTo` and `validateStartResearch` share one test (`closedBy`), which also refuses a project whose `requiresProgramStage` exceeds the company's completed stages (`TechNeedsProgramStage`); starting a stage's project raises a public `ProgramResearchStarted` notice.
- `events.cpp` — step 8: `eventMultiplier` (product of active effects clamped to [0.01, 100]) and `runEvents` (expire, then one roll per event per day on `state.rng.event`).
- `tech.cpp` — `techMultiplier` and `resourceUnlocked` over a company's completed techs.

World, views, AI
- `worldgen.cpp` — `generateWorld` (plots, land prices, hidden log-normal deposits from `state.rng.world`), listing-queue construction (`PlotPool`, `drawOpeningHand`, `drawRemainder`), `classifyRegionAtStart`.
- `view.cpp` — `makeCompanyView` and its helpers (`makePlotView`, `addSales`, `addOwnSurveys`, `addMineOpexQuotes`, `addOwnMines` with `addMineEconomics`, `addMarkets`, `addOwnLoans`, `addResearchAndEvents`, `addResearchOpen`, `addUnlockProgress`, `addCompanies`, `addOwnProgram`, `addRace`).
- `ai_controller.cpp` — `HeuristicController::decide`: nothing when bankrupt or the game has ended; one shared `Budget`, then the race first (`decideStage`, which stops there when its order founds the program; `decideReserves`; `decideDeliveries`), then `decideSales`, `decideMothballing`, `decideBuilding`, `decidePlots`, `decideResearch` (`programResearchGoal` when the program needs a project, otherwise `ordinaryResearch`), `decideLoans`/`decideRepayment`; plot valuation in `valuedPrice` and `surveyedNetValue`, the latter scaled by `ai.programOreLandWeight` for ores the program needs and the company's mines will not supply (`programShortOf`); hash-based `bidJitterPercent`.

Persistence
- `save.cpp` — `hashWorld`, `hashContent`, `saveGame`, `loadGame`, and `validateWorld` with its per-entity validators (deposits, companies with their program stage, deliveries and stage days, the outcome (`validateOutcome`: an open race, or a founding or passed deadline consistent with the companies and `programResultBesideFounder`), plots, auctions and bids, listing and withdrawn queues, surveys, mines with their loss fields, markets, loans, research, events).

Foundation
- `fixed.cpp` — 128-bit `mulDiv`/`checkedMulDiv`/`compareProducts`, overflow-checked add/sub/negate, `Fixed::fromDouble`, `toString(Fixed)`.
- `money.cpp` — `toString(Money)`.
- `math.cpp` — `clampFixed`, `fixedLn`, `fixedExp`, `fixedPow`.
- `rng.cpp` — `Pcg32` constructor, `next` (XSH-RR), `below` (Lemire rejection method).
- `random.cpp` — `RngStreams(seed)` with stream constants 1..5, `uniformFixed`, `uniformInt`, `approxNormal` (sum of twelve uniforms minus 6).
- `content.cpp` — `Content::findResource`/`findTech`/`findRegion`/`findEvent` linear lookups by string id.
- `fatal.cpp` — `fatal`: writes "sim fatal error: <message>" to stderr and calls `std::abort`.

## Dependencies
- Only the public headers in `sim/include/sim/` and the C++ standard library (`<algorithm>`, `<cstdint>`, `<optional>`, `<variant>`, `<vector>`, `<string>`, `<limits>`, `<utility>`, `<array>`, `<span>`, `<new>`, `<cassert>`, `<cmath>`, `<cstdio>`, `<cstdlib>`, `<string_view>`, `<cstddef>`).
- `fixed.cpp` additionally uses compiler `__int128` or MSVC `<intrin.h>`.
- No includes from `content/`, `platform/`, `game/`; no nlohmann-json and no file I/O. No source file here opens `data/*.json`; that is done by `content/src/loader.cpp`, which hands this library a filled `sim::Content`.
- Cross-file calls: `simulation.cpp` calls every rule system, including `program.h`; `program.cpp` calls `research.h` (`techCompleted`) and `plot_market.h` (`availableCash`); `save.cpp` calls `program.h` (`programResultBesideFounder`) and `research.h`; `finance.cpp` calls `markets.h` (`executeSale`, `recordAutoSale`) and `mines.h` (`warnIdleMines`, `warnRunningLow`); `markets.cpp` (DemandMult), `mines.cpp` (OpexMult) and `plot_market.cpp` (LandPriceMult) call `events.h` (`eventMultiplier`); `surveys.cpp`, `mines.cpp` and `finance.cpp` include `plot_market.h` for `availableCash`/`reservedCash`; `view.cpp` calls `finance.h`, `mines.h`, `plot_market.h`, `program.h`, `research.h`, `surveys.h`, `tech.h`; `ai_controller.cpp` reads only `CompanyView` and `Content`.

## Patterns
- One translation unit per rule system; file-local helpers and constants live in anonymous namespaces.
- Validate-then-apply: `applyX` functions assume a command that `validateX` has accepted.
- Each system appends `Notice` values to the tick's `TickReport` rather than returning results.
- Month-end work is gated on `state.day % kDaysPerMonth == 0` (and day > 0 in `runFinance`).
- Tuning numbers come from `Content::balance`, `Content::ai` and `Content::program`, including the market clamps; the few in-code constants are safety clamps (noise draws to three sigma) or fixed rules (`kLesserPartnerStages` = 2 in `program.cpp`).
- The AI is stateless: its decisions depend only on the view and content, so no controller memory is persisted. `RngStreams::ai` is seeded and serialized but no source file in this directory draws from it.

## Constraints
- Pipeline order in `Simulation::tick` is fixed: 1 commands, 1b program deadline (`runProgram`), 2 plot market, 3 surveys, 4 mines, 4b sell policies, 5 markets, 6 finance, 7 research, 8 events, 9 day increment. It aborts when `state.day >= kMaxDay`.
- The game ends in step 1 (an order completes the last stage) or 1b (the deadline day); the tick returns there, so no later step runs and the day does not advance. A tick of an ended world only applies, and so refuses, queued commands.
- Commands are applied sorted by company id with `std::stable_sort`, preserving submission order within a company.
- `view.cpp` must never read the hidden deposit state; its first line notes that a test greps the file for it.
- `runEvents` rolls for every event every day so the event stream advances identically whatever is active.
- `techMultiplier` accumulates in content order, not completion order, because fixed-point rounding is order-sensitive.
- Sales made after the market step (the month-end cover-costs rule) must pass `countSupply = false` to `executeSale`.
- `debitCash` saturates at `-kMaxCashCents` so compulsory charges keep cash inside the persisted bound.
- `loadGame` rejects bad magic, unknown version, content-hash mismatch, truncation, trailing bytes, or a world failing `validateWorld`, and never throws (`validateWorld` converts `std::bad_alloc` to false).
- `<cmath>` is used only by `Fixed::fromDouble` (`std::llround`); no other floating point appears in sim logic.
- `uniformInt` checks its range preconditions with `assert` only; other invariants use `fatal`, which aborts in every build type.
