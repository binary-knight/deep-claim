# Deep Claim — Domain Briefing

<!-- This file is the most important file in the repository. It answers every
     question an agent or engineer should ask before touching code. All
     architecture, implementation, and prioritization decisions trace back here.
     Drafted 2026-10-10 from the repository's own documents; each claim cites
     its source in brackets. -->

---

## Domain

Deep Claim (working title) is a single-player management game for desktop
computers in which the player runs a mining company: buying land at auction,
surveying it, building mines, selling ore into moving markets, borrowing,
researching, and competing with AI-run rival companies. It belongs to the
business-simulation ("tycoon") genre of PC games. The long-term vision is a
deep but approachable simulation, easy to learn and hard to master, that
begins with mining on Earth and grows, once the player is rich enough, into
a space program and asteroid mining, with military and espionage rivalry
between companies. Only the Earth game is being built now. [README.md;
docs/DESIGN.md "Architecture split"; owner, 2026-10-10]

---

## Core Problem

The game has to make running a mining company a contest of decisions for a
whole ten-year play session. Today it does not. Three monitored playtests
each ended the same way: once the first good mine is running, money stops
being a constraint and the player has almost nothing left to decide (33
orders in eight years in playtest 3, including 900 days without a land
decision). Every market is flooded to five times demand, prices sit at 0.35
to 0.5 times base from year three, and mines stay profitable anyway, so
nothing stops either company from building. The rival is a peer only against
another AI. Until this is solved on Earth, the game is a working simulation
without a strategy game on top of it. [docs/playtests/2026-10-10.md findings
2 to 4; docs/specs/006-balance.md "Goal"; owner direction 2026-10-10 in
docs/HANDOFF.md]

---

## How Deep Claim Makes Money

It does not yet. Deep Claim is an unreleased, privately developed game with
one developer-owner and no customers. The intent is a paid release: a
one-time purchase on a PC storefront (Steam, with the Steam Deck as a target
device), with no date set. The player who buys the game is the only paying
party; there are no subscriptions, in-game purchases or advertising. The
`platform/` library is already shaped like a storefront service interface
(saves, achievements, statistics) with only a local implementation. Until
release, "value" means the owner's playtest verdict: is the game fun,
challenging and strategic. [owner, 2026-10-10; docs/DESIGN.md "Architecture
split"; platform/include/platform/services.h; docs/design/ui-design.md]

---

## What Deep Claim Does

The player and one or more AI rivals each run a company in a generated world
of 120 plots across three regions (Northern Shield, Copper Belt, Salt Flats)
holding four resources (iron, copper, nickel, lithium). True deposits are
hidden; a company learns about a plot only by paying for a survey, whose
result carries error that research reduces. Plots are listed over time and
sold by timed auction. A company builds one mine per owned plot, pays capex
up front and opex daily, and extracts ore until the deposit is exhausted.
[data/regions.json; data/resources.json; data/balance.json; docs/DESIGN.md
"Hidden truth vs observable view", "Surveys and mines"]

Ore is sold into one market per resource. Price moves with supply against
demand, so companies depress the prices they sell into. Standing sell
policies sell automatically above a floor and a reserve; manual sales and a
cover-costs rule exist beside them. Finance covers loans with monthly
instalments, storage charges, and bankruptcy after a grace period. A
ten-project research tree improves surveys, extraction and logistics and
unlocks lithium. World events shift demand and costs. [docs/DESIGN.md
"Commodity markets", "Finance", "Research, events and the AI";
docs/specs/001-sell-policies.md; data/techs.json; data/events.json]

One tick is one in-game day in a 360-day year. Each day the simulation
applies queued commands, resolves auctions, advances surveys, runs mines and
sell policies, updates markets, does month-end finance, advances research and
events, then reports notices to each company. The player sees only a
per-company view and a log; the interface sends commands and never touches
world state. [docs/DESIGN.md "Determinism rules"; sim/src/simulation.cpp]

Three actors use the same simulation: the player through the SDL3 and RmlUi
client; the built-in AI, which plays rivals and, in the headless
`sim_runner`, every company for balance sweeps; and the developer, who reads
playtest traces and replays them exactly. [README.md "Balance runs";
docs/DESIGN.md "Monitored playtesting"]

---

## Operational & Regulatory Constraints

No regulatory regime applies: the game is offline and single-player, has no
accounts, no network code, no payments and collects no personal data.
[baseline seam survey 2026-10-10: no network or environment seams found]

The non-negotiable constraints are technical and self-imposed:

- **Determinism.** The same seed and command log must produce the same state
  hash on Linux/GCC and Windows/MSVC. No wall clock, no floating point in
  state, own PCG32 random streams, no unordered iteration during a tick. A
  golden hash is checked in CI. [docs/DESIGN.md "Determinism rules";
  tests/determinism_test.cpp]
- **Hidden information.** Views and AI controllers never read true deposits.
  [docs/DESIGN.md "Hidden truth vs observable view"; tests/view_source_test.cpp]
- **Platforms.** Linux and Windows desktop, with a 1280x800 Steam Deck frame
  as a legibility target (14px minimum text, 40px controls). CI builds both
  operating systems; sanitizers run on Ubuntu only. [docs/design/ui-design.md;
  .github/workflows/ci.yml]
- **Licences.** Bundled fonts are under the OFL and the vendored RmlUi
  backend is MIT; their licence files ship with the assets.
  [game/assets/fonts/; game/backends/]

---

## Product Core

| Term | What it is | What it is not |
| --- | --- | --- |
| Company | A competitor with cash, loans, plots, mines, stock and research; the player is company 0. | Not a user account. |
| Plot | A parcel of land in a region, with a hidden deposit, sold by auction. | Not a mine; a plot may hold nothing. |
| Deposit | The true tonnage of each resource under a plot, known only to the sim. | Never shown directly; players see survey estimates. |
| Survey | A paid, timed estimate of a plot's deposit, with error. | Not the truth, and not shared with rivals. |
| Auction | A timed sale of a listed plot to the highest bid at or above the ask. | Not negotiable; unsold land is relisted cheaper, then withdrawn. |
| Mine | The works on an owned plot: building, operating, mothballed, depleted. | Not transferable; one per plot. |
| Resource | Iron, copper, nickel or lithium ore, counted in whole tonnes. | Not refined product. |
| Market | The price of one resource, set by smoothed supply against demand. | Not an order book; there are no counterparties. |
| Order (command) | An instruction from a company, applied at the next tick or rejected with a reason. | Not an immediate state change. |
| Notice | A fact the sim reports to one company or to all, shown in the log. | Not free text; the wording comes from the strings catalogue. |
| Sell policy | A standing rule to sell a resource above a floor price, keeping a reserve. | Not a one-off sale. |
| Loan | Borrowed principal repaid monthly with interest, within a capacity. | Not equity. |
| Tech | A research project bought once, with prerequisites. | Not repeatable. |
| Event | A timed world condition that shifts demand or costs. | Not caused by players. |
| View | What one company is allowed to know at a given day. | Not world state. |
| Trace | The record of a play session, enough to replay it exactly. | Not a save file. |

[sim/include/sim/commands.h; sim/include/sim/view.h; docs/DESIGN.md;
game/session/include/game/trace.h]

Frequently confused: "order" is the player-facing word and "command" the
code's word for the same thing; "pending order" (spec 007) means given but
not yet applied; "spendable" or "available" is cash less money reserved for
standing bids.

---

## What Deep Claim Is Not

Most of these are boundaries of order, not of ambition: the long-term game
is larger than what is being built now, and the sequence is deliberate.
[owner, 2026-10-10]

- **Not a space game yet.** The whole Earth mining game comes first and must
  be fun, challenging, rewarding and hard to put down on its own. Space is
  the late game the player earns: once they have amassed enormous wealth they
  slowly build a space program, with asteroid mining as the eventual goal.
  No space or asteroid design is drafted until the Earth game holds up, and
  no Earth problem is parked for a space phase.
- **Not multiplayer yet.** Today the game is single-player and offline
  against AI-run companies. Play against other people is a future aim, so
  nothing should be built that makes it impossible (the deterministic,
  command-driven sim is what keeps that door open), but no network code is
  in scope now.
- **Not yet a game of conflict.** A military and espionage layer between
  companies, against AI rivals first and other players later, is intended
  but not designed. Rivals today compete only through auctions and markets.
- **Not yet a deep geological, engineering or systems simulation.** That
  depth is where the game is headed: relatively complex underneath, with a
  gentle learning curve. Today deposits are tonnages, a mine is one unit per
  plot and there is no logistics map. Depth is added where it creates
  decisions, never as detail for its own sake.
- **Never hard to learn.** Easy to learn, hard to master. A system the player
  cannot grasp from the screen in front of them is out of scope however
  realistic it is.
- **Not a real-time action game.** The clock pauses and every decision can
  be made with it stopped. This one is permanent.
- **Not a financial-markets simulator, for now.** One price per resource; no
  order book, futures or shares. Not ruled out, not planned.
  [docs/DESIGN.md "Commodity markets"]

Open question, deliberately held: whether any game logic may live in the
interface. The working rule until it is revisited is R-001 in
`ARCHITECTURE.md` (the sim is the authority; the client may derive display
state).

---

## Risk Posture

Ranked by severity:

1. **The game is not fun.** A long session with no meaningful decisions is
   the failure all three playtests found; every other risk is secondary to it
   while the game is unreleased. Recoverable, and the current priority.
   [docs/playtests/]
2. **Non-determinism.** If a trace stops replaying to the same hash, playtest
   analysis, balance sweeps and cross-platform saves all lose their footing.
   Silent and hard to trace back, so treated as unrecoverable: the golden
   hash and replay divergence checks must never be weakened. [docs/DESIGN.md]
3. **Leaking hidden deposits** into a view or the AI. It would quietly remove
   the survey gamble the game is built on. [tests/view_source_test.cpp]
4. **Save corruption or silent load of a mismatched save.** Before release,
   breaking old saves is accepted and has been done on purpose (content
   fingerprint, save version 1); loading a wrong save without refusing is
   not. [docs/DESIGN.md "Content and data files"]
5. **Balance changes made blind.** A tuning change without a sweep can bring
   back bankruptcies or the snowball; balance work is measured with
   `sim_runner --sweep` before and after. [docs/specs/006-balance.md]
6. **Machine-sounding writing.** Player-facing text that skips the polish
   pass undermines the voice the owner chose. Merely expensive.
   [docs/process.md "Prose"]

---

## Design Implications

- Because playtest analysis and saves depend on exact replay, the system must
  keep all state integer or fixed-point, draw only from seeded streams, and
  change the golden hash only deliberately, with the reason recorded in
  `docs/DESIGN.md`.
- Because the AI, the balance runner and the player must play the same game,
  every rule must live in the sim and be enforced when a command is applied;
  the client and session may derive display and button state from the view
  but never a rule the sim does not also enforce. [R-001]
- Because surveys are a gamble, nothing outside `WorldState::hidden` may
  carry true deposits, and views must be built without reading it.
- Because the game is tuned by measurement, numbers a designer would tune
  must live in `data/*.json`, and a balance change must come with a sweep.
  [R-003]
- Because the writing has a chosen voice, text the code shows the player must
  come from `data/strings.json` after the draft-and-polish pass. [R-002]
- Because the core problem is the absence of decisions, a feature must be
  judged by whether it adds a real choice for the player on Earth, and it is
  not done until a playtest shows that it did.
- Because the game targets a Steam Deck frame as well as desktop, interface
  work must follow the locked tokens and pass the layout probe at both frame
  sizes. [R-004]

---

*This document is the source of truth for domain understanding. Any implementation
decision that contradicts this document must be flagged and resolved before proceeding.*
