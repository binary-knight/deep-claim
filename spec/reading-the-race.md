# PRD: Reading the race (spec 011, the follow-up to spec 010 step 1)

Status: approved by the owner, 2026-10-18; designed, built, reviewed and remediated on branch spec-011-reading-the-race (to `31a05de`), not merged; awaiting playtest 6.

## Summary
Playtest 5 (`docs/playtests/2026-10-18.md`) played spec 010's race to its end and lost it without the player knowing they were in one. The sim's race works; the screen does not tell it. Three things went wrong. The player read "Behind" and "29 years left" as plenty of time, when the race had been decided on research time by day 2406 (finding 1). Stage 2 needed nickel, and the player could not find out where it comes from (finding 2). And the clock stopped 58 times without saying why, the rival's six race stops looking like the 52 routine ones (finding 4).

This spec makes the race readable. Each company gets an earliest founding day, worked out by the sim from the research clocks that pace the race, and the race displays lead with it: how far behind the player is in days, the last day to start the next stage's research and keep pace, and a notice that stops the clock on the day the player falls behind. The Program tab says where each ore the program needs comes from, and the Plots tab marks land that may hold it. A stop keeps its reason on screen until the clock is started again, and a race stop looks like one. Three small playtest 5 fixes ride along.

It changes no balance number: no research clock, stage cost, deposit odds or AI knob. Spec 010's step 2 (plants) waits for it (spec 010, BR-025; the owner, 2026-10-18). It serves journey J-003, steps 2, 5 and 6, against two of its failure modes: "they do not notice the rival pulling ahead" and "they ignore an ore the last stage needs until the rival holds the land". Saves and traces from earlier builds stop loading.

## Scope
### In Scope
- An earliest founding day for every company, in the view; the race verdict ranks on it.
- The race strip, the status strip and the Program tab lead with it.
- A notice and a clock stop when the player falls behind on research time.
- Where each needed ore comes from, on the Program tab; a Needs you row and Plots tab marks for an ore the company has no source of.
- Clock stops that explain themselves, and race stops that stand out.
- Ride-alongs from playtest 5: cover-costs sales spare program stock; the end screen's mine count; a standing bid shown as standing.
- The baseline's open R-001 item in the client code this spec touches: the storage-cost warning moves into the sim.

### Out of Scope
- Pacing: research durations, stage costs, the cash curve. Spec 010's step 4 tunes them after plants give money a second use (the owner, 2026-10-18).
- Nickel supply and the AI's land buying. The spec shows where nickel is and leaves the land fight as it is (the owner, 2026-10-18).
- The running-low warning's accuracy (playtest 5, finding 5). It is evidence for spec 010's step 3, mine life.
- A survey that finishes after its plot is withdrawn (finding 7).
- Plants and goods (spec 010, step 2).

## Requirements

### BR-001: Earliest founding day
For every company the sim works out the earliest day it could found the program, assuming that every remaining stage project starts as soon as the rules allow and that goods and cash never hold it up. The lab runs one project at a time and a project cannot be cancelled (`sim/include/sim/research.h`). So the figure is the finish day of the current stage's project if it is under way. If it has not started, the figure counts from the day it could start: today, or the day the lab finishes the project it is busy with. To that it adds the research duration of each later stage, counting the days orders take to apply as the sim applies them. A company that has founded the program has its founding day. The figure is public, like the rest of the race (spec 010, BR-007), and is in every view with the finish day of each company's current project. It is derived, not saved.

Since no research can be sped up, the figure never moves earlier. Each day a project could have started and did not moves it a day later; starting the project stops it moving. A lead shown in days is one the rival cannot take back on research time. It can still be lost to goods and cash, which the figure leaves out.

### BR-002: The verdict
Ahead, Behind and Level are decided by earliest founding day first, then by stages complete and share delivered. The verdict gives the gap in days ("Behind by 1,374 days"). When the player's earliest founding day falls after the deadline, the verdict says the program can no longer be founded in time. Today the verdict ranks by stages and deliveries only, so a player who has delivered more but not started the research reads Ahead while losing on time.

### BR-003: The race displays
The status strip's race item keeps the time left to the deadline, as spec 010's BR-001 requires. Ahead of it, when a rival's earliest founding day comes before the deadline, it gives the time left before that rival could found the program. In playtest 5 the deadline alone ("29 years left") read as time to spare. The Dashboard's race strip gives each company's earliest founding day and, for research under way, the day it is due.

### BR-004: Keeping pace
While the player's current stage research has not started, the Program tab gives the last day it can start and still let the player found the program no later than the rival's earliest founding day, or says that day has passed. When the lab is busy with another project, it says so and gives the day the lab is free. From a set number of days before the last day, the Dashboard's Needs you lists it. That number is `program_pace_warning_days` in `data/program.json`, provisionally 360. In playtest 5 the player's cash stayed under stage 2's $10M research cost for about 1,200 days after stage 1 (day 1886 to about day 3100), so a short warning would be no use. A year gives time to borrow, and $10M of credit went unused all game.

### BR-005: Falling behind is news
On the day the player's earliest founding day passes a rival's, the player gets a notice that names the rival and gives both days, and the clock stops under a new Stops rule, on by default. A bankrupt rival is left out. The notice comes once each time the player falls behind, and again only after they have drawn level or ahead. A notice also comes on the day the player's earliest founding day passes the deadline.

### BR-006: Where each ore comes from
For each good that any stage not yet complete needs, the Program tab gives where it comes from:
- the regions that can hold it, with their odds, from `data/regions.json`;
- the research that unlocks it, where it is locked;
- the player's mines that yield it, with their daily output;
- the plots where the player's surveys show it.

### BR-007: A missing ore
An ore is missing when the stages not yet complete still need more of it than the player holds and none of the player's mines yields it. A missing ore that the current or the next stage needs is a row in the Dashboard's Needs you, saying where it is found, with a button to the Plots tab. Rows stop at the next stage so that Needs you does not open the game with every ore stage 3 asks for; the Program tab and the Plots tab marks cover all the stages.

### BR-008: Land marked for the program
On the Plots tab, a listed plot in a region that can hold a missing ore carries a mark naming that ore. A plot whose survey by the player shows a missing ore carries a stronger mark.

### BR-009: Stops say why
When a notice stops the clock, its reason stays on screen until the clock is started again. It shows:
- which kind of stop it was, by the Stops rule's name;
- the notice's own line;
- a button to the tab where the player acts on it;
- a way to switch that rule off.

When several notices stop the clock on one day, it names the first and says how many more there are. In the log, every line that stopped the clock is marked.

### BR-010: Race stops stand out
A stop for a rival's program progress (spec 010, BR-010) or for falling behind (BR-005) is shown as a race stop. It has its own heading and gives both companies' earliest founding days. It is set apart by words and placement, not by colour alone (`docs/design/ui-design.md`).

### BR-011: Cover costs spare program stock
The month-end cover-costs sale sells, of each good, the stock above what the current stage still needs before any of the stock the stage still needs. Within each of those two tiers it keeps today's order, most valuable stock first. The Program tab's cover-costs warning says so.

### BR-012: The end screen counts open mines
The end screen's Mines figure counts the player's mines that are not worked out.

### BR-013: A standing bid is shown
A listed plot on which the player's bid stands shows the bid in the plot panel and in its row of the plots-for-sale table. A second bid is labelled as replacing it.

### BR-014: The storage warning moves into the sim
The storage-cost warning's threshold, a tenth of cash, moves into `data/balance.json` as `storage_warning_cash_share`, 0.1, and the decision into the sim's view. The value is unchanged. This clears the R-001 item on `game/src/ui_model.cpp:781-783` in `ARCHITECTURE.md`'s do-not-copy list, which the owner set for the next client task. Labels hard-coded in `ui_model.cpp` that this spec touches move to the catalogue (R-002).

### BR-015: No balance change
No existing value in `data/*.json` changes. The only new keys are the two this spec names: `program_pace_warning_days` (BR-004) and `storage_warning_cash_share` (BR-014). The gated sweep is run before and after, because BR-011 applies to company 0 in the runner, whose cover costs are on.

### BR-016: Determinism
Earliest founding days are integer arithmetic with no saved state. BR-005's once-per-fall rule and BR-011 change state or behaviour, so the golden hashes move once each, with the reason in `docs/DESIGN.md`. Older saves and traces are refused.

### BR-017: Prose
Every new string goes through the prose pipeline.

## Acceptance Criteria
1. **AC-001 Earliest founding.** Sim tests: for research not started, under way and done, with the lab free and busy with another project, and for a founded program, the earliest founding day matches the rule in BR-001 against `data/techs.json`'s durations. A company's day does not change when only its deliveries or cash change, and never moves earlier from one day to the next. Every view carries every company's day and the finish day of its current project.
2. **AC-002 The verdict.** Tests: a player with more delivered but research not started, against a rival with research under way, reads Behind with the gap in days. An earliest founding day after the deadline gives the "cannot found in time" verdict.
3. **AC-003 Falling behind.** Sim tests: the notice is raised on the day the player's earliest founding day passes the rival's, names the rival and both days, and is not raised again until the player has drawn level or ahead. A bankrupt rival raises none. The deadline notice is raised once. The session test shows the clock stops under the new rule, on by default.
4. **AC-004 The player sees how far behind they are.** As the player, navigate from any tab via the status strip's race item and the Dashboard's race strip to see both companies' earliest founding days and the gap in days, to know whether they can still win. Live at build. Fully functional means: the figures equal the sim's, change on the day a research start is applied, and the status item names the rival's earliest founding when it comes before the deadline.
5. **AC-005 The player knows when to start research.** As the player, navigate from the Dashboard's Needs you row or the Program tab to the last day to start the current stage's research and keep pace, to start it in time. Live at build. Fully functional means: the day shown equals the rival's earliest founding day less the player's remaining research; the row appears `program_pace_warning_days` before it; a busy lab is named with the day it is free; once the day has passed, the tab says so.
6. **AC-006 The player finds the ore the program needs.** As the player, navigate from the Program tab's "where from" lines, or from the Needs you row for a missing ore, via the Plots tab's marks, to plots that may hold that ore, to survey and bid for it. Live at build. Fully functional means: the regions and odds equal `data/regions.json`'s; the marks appear only for ores that are missing as BR-007 defines; a surveyed plot showing the ore carries the stronger mark; the player's mines that yield the ore are listed with their output.
7. **AC-007 The player knows why the clock stopped.** As the player, with the clock at its fastest, navigate from a stop's reason on screen via its button to the tab where it is acted on, or switch its rule off from there, to decide what to do. Live at build. Fully functional means: the reason stays until the clock is started again; it names the rule as the Stops menu does; several stops on one day show the first and a count; lines that stopped the clock are marked in the log; a race stop has its own heading and both earliest founding days.
8. **AC-008 Cover costs spare program stock.** Sim tests: with a shortfall that free stock covers, no stock the stage needs is sold; with a larger one, free stock of every good goes before any stock the stage needs; with no program need, the order is today's.
9. **AC-009 Small fixes.** Tests: the end screen's mine count leaves out worked-out mines; the plot panel and the plot's row show a standing bid; the storage warning comes from the view and its threshold from data.
10. **AC-010 Green.** All presets pass. Format and tidy are clean. The layout probe passes at both frames with the new displays filled, including a long rival name and a stop with a count. The gated sweep exits zero on seeds 1 to 50 and 51 to 100, and its figures are recorded before and after. Golden hashes move once each with the reason in `docs/DESIGN.md`.
11. **AC-011 Prose.** Every new string is in the catalogue through the pipeline, and the parity test passes.
12. **AC-012 Played.** A monitored playtest, written up in `docs/playtests/`, after which the owner can say how far behind they were and when, what the last day was to start each stage's research, where nickel was to be found, and why the clock stopped each time it stopped. Whether they won is not the test.

## Edge Cases
1. Neither company has started its current project. Both earliest founding days move a day later with each day that passes. The gap between them holds, and the last day to start moves with the rival's.
2. The rival finishes a stage and waits before starting the next project, as the AI does while it gathers deliveries. Its earliest founding day moves later each day, and a player whose project is under way sees their lead grow. The lead is real on research time (BR-001): starting the project stops the rival's day moving, it does not pull it back.
3. The player's lab is busy with a project outside the program. The current stage's project cannot start until it finishes, and the earliest founding day and the last day to start count from then (BR-001, BR-004).
4. The rival is bankrupt. It has no earliest founding day for the race displays, the last day to start and BR-005. The player races the deadline, and the last day to start is measured against it.
5. The player and the rival have the same earliest founding day. The verdict is Level; the lower company index would be applied first (spec 010, edge case 1), and the verdict does not try to say so.
6. The last stage is under way for both. The earliest founding days are the projects' finish days, and the verdict is the gap between them.
7. The player falls behind and draws level on the same day, through the rival's order and their own. BR-005 compares the days at the end of the tick: one notice or none.
8. An ore is locked by research (lithium). "Where from" names the research that unlocks it. It counts as missing from the first day, so its land is marked, but it gets a Needs you row only once the current or the next stage needs it.
9. A mine that yields a needed ore is mothballed. It still counts as a source: the player can reopen it.
10. The program is founded, or the game has ended. The race displays give the outcome as today; no notices, rows or marks.
11. A saved game from before this spec. Refused, as with any change to saved state.

## Research Notes
- **Who got the nickel.** Playtest 5's world (seed 9199558957000, regenerated outside the repo) held 13 nickel plots: eleven in the Northern Shield and two in the Copper Belt. Two of them, #17 and #19, hold nickel and no iron. Northfield bought 12 of them. #19 never sold. Four went unsold before Northfield bought them or the game ended: #17 (day 1290), #40 (day 1980), #19 (days 2550 and 5280) and #9 (day 3360). The player's 24 surveyed plots included none of the 13. Had they picked as many plots at random in each region (seven in the Northern Shield, ten in the Copper Belt), the chance of missing every nickel plot was about 5%.
- **The odds were on screen.** An unsurveyed plot's survey column reads "Region odds stand at …", nickel included (`ui.survey_blind`). The owner did not take it in, and nothing linked those odds to the Program tab.
- **The verdict.** `raceStanding` in `game/src/ui_model.cpp` ranks by stages complete, then share delivered. Research, which paces the race (spec 010 findings F5 and F7), is not in it.
- **The rival's research.** The public view has its research state (not started, under way, done) but not its finish day (`sim::ProgramPublicView`).
- **The stop line.** `App::update` writes "Paused: …" to the status line and clears it 12 seconds of real time later (twice `kStatusSeconds`), whether or not the clock is still stopped. Any later status message replaces it sooner.
- **Cover costs.** `coverCosts` in `sim/src/finance.cpp` sells the most valuable stock first, with no regard to the program. In playtest 5 that was the copper held for stage 1, on days 1170, 1230 and 1260. The AI's companies have cover costs off; the runner's company 0 is made as the player and has them on.
- **The research clocks.** Spec 010's stage projects take 480, 1,680 and 2,520 days (`data/techs.json`), so a founding lies at least 4,680 days after the first project starts.

## Decisions
Owner, 2026-10-18, before the draft:
- The follow-up is its own spec, number 011, drafted from the playtest evidence and not through the `/spec` interview.
- Legibility only: no change to the race's pacing. That waits for spec 010's step 4.
- Nickel: show where it is. Supply and the AI's land buying are unchanged. The risk is accepted that playtest 6 ends without nickel if Northfield outbids the player.
- Ride-alongs: cover-costs spares program stock, the end screen counts open mines, a standing bid is shown.

Owner, 2026-10-18, on approving the draft:
- The spec is approved as drafted.
- Stop defaults are unchanged: every stop stays on and each explains itself. In playtest 5 the 58 stops included 20 surveys and 12 research projects. Spec 004 turned those two on because an unseen report or an idle lab cost the player more than a stop.
- Earliest founding days are shown as time from now in the status strip ("in 6 y 120 d") and as dates ("Y17 M5") on the Dashboard and the Program tab, as the game gives dates elsewhere.

## Build (2026-10-19)
Built on branch `spec-011-reading-the-race`, tasks 001 to 016 and remediations 017 to 021, commits `5b03b46` to `31a05de` and this record on base `786e9bd`. Not merged. The remediations: 017 fixed a release-only compiler warning (`6de2775`); 018 made the layout probe clang-tidy clean (`9a1b04d`); 019 applied the review fixes in the sim (`32d3109`); 020 applied them in the client (`68dc4c6`); 021 fixed the Codex re-review's finding (`31a05de`). The requirements and acceptance criteria above are left as approved; where the build differs from them or from the design (`.etc_sdlc/features/active/F-2026-10-18-reading-the-race/design.md`), the difference is listed under "Departures". The architecture, tick step 9b, the golden hash move and the gated sweep's figures are in `docs/DESIGN.md`, "Reading the race (spec 011)". The decision records are `docs/adrs/F-2026-10-18-reading-the-race-001` to `-004`. The departures were accepted by the build conductor, not by the owner; four are marked for the owner to confirm.

Reviews (texts in `.etc_sdlc/features/active/F-2026-10-18-reading-the-race/review/`):
- Codex review (`codex-spec011.md`): five findings, all applied.
  - P1, a busy lab after the stage research is done was not counted (019).
  - P1, an ore a mine had worked out still counted as a source (019).
  - P2, the verdict raced a bankrupt rival, and the client ranked rivals itself. 019 adds `ProgramView::leadingRival` and `leadingRivalFoundingDay`; 020 reads them and drops the stages-and-share fallback. With no solvent rival there is no Ahead, Behind or Level (edge case 4).
  - P2, a fresh world past its deadline could not load its own save before the first tick. 019 settles the pace flags in the `Simulation` constructor.
  - P3, AC-004's navigation was missing. 020 makes the status strip's race item open the Dashboard and the race strip open the Program tab. The layout probe clicks both, and the pace and missing-ore Needs you buttons.
- Codex re-review (`codex-spec011-rereview.md`): all five confirmed fixed. One new P2, applied in 021: a mothballed mine that mined copper while lithium was locked stopped counting as a lithium source once lithium was unlocked. A mine now yields an ore when its last operating day produced it, or when the mine never extracted it and the owner's best survey shows it.
- Code review (code-reviewer agent): gate pass.
  - Medium, the busy lab after research: the Codex P1, applied in 019.
  - Medium, the client re-implemented the leading rival: applied in 019 and 020.
  - Low, the plot marks' two conditions are worked out in the client ("region can hold" is odds above zero; "survey shows" is an estimate above zero). Declined: they assemble a display from content and the player's own survey view. They are not a rule the sim enforces or decides, and R-001 allows display derivation.
  - Low, GA-008's disclosure of a rival's busy lab: already the owner's decision.
  - Low, the unused `status.paused_by` and the stale `.meta` descriptions: open, below.
- Security review: gate clean, no findings.
- Spec-enforcer, in two chunks. Before the fixes, AC-001, AC-003, AC-004 and AC-006 were not met, on the defects above, and AC-002 and AC-009 lacked evidence: no bankrupt-rival pass, no worked-out mine at the end screen. After them, 020 added an end screen pass with a worked-out mine. A bankrupt-rival probe pass was not added (open, below). AC-012 cannot be met until playtest 6.
- Architect-reviewer: the agent the review gate calls for is not installed in this session. Its skip is recorded; the Codex review covered the architecture.

Verification at `31a05de` (an earlier one at `6de2775` is superseded):
- Tests: every preset builds with no warning and passes `ctest`. Debug 480 of 480, release 480 of 480, asan 480 of 480, debug-game 481 of 481 and release-game 481 of 481; both game presets run the layout probe at 1366x820 and 1280x800.
- Format and tidy: clang-format 18 clean on every changed C++ file. clang-tidy 18 clean on every changed shipped source and on the layout probe, 0 findings.
- Golden hashes: moved once, in task 004 (`6f6a81d`), for the two pace flags on every company. `kGolden` `0x059a7c79213dd307` to `0xc7cc092323bf84f7`, `kGoldenPlayed` `0x41192679894c4afc` to `0x43349088476870b5`, `kGoldenRace` `0xae14b05ab3e56760` to `0xf576ea4384386ae7`. Tasks 006, 019, 020 and 021 moved none. Recorded on Linux/GCC; CI confirms Windows.
- Gated sweep, release preset, `sim_runner --sweep 50 --years 30 --ai 1 --sanity`, before at `786e9bd` and after at `31a05de`. Seeds 1 to 50 and 51 to 100 exit 0, and the JSON output of both is byte-identical before and after. No figure moved, so BR-015 holds.
- Sweep time: absolute times vary with machine load (other sessions; load average 6 to 12), so the old and new runners were timed back to back on seeds 1 to 50. 40.70 s old against 41.86 s new, then 45.78 s against 46.81 s: about 2 to 3% slower, within the 5% budget.

Departures (build decisions; the requirements and criteria above are not rewritten):
- 2026-10-19, BR-016 and the design: the golden hashes moved once, not twice. The cover-costs change (task 006) moved none of them, because cover costs never runs in the three golden games. Its effect is measured by the gated sweep instead, where nothing moved.
- 2026-10-19, design: `Session::stopsToday` is also cleared at the start of each tick, not only when the clock starts and on load, so it always holds the day's own stops (task 009, `e0eebfe`).
- 2026-10-19, BR-009: the game-ending log line is marked `stoppedClock` though it matches no Stops rule. BR-009 marks "every line that stopped the clock", and the end of the game stops it under any rule set (task 009, `e0eebfe`).
- 2026-10-19, BR-002 and edge case 5: the verdict reads Level on equal earliest founding days even when deliveries differ. BR-002's "then by stages complete and share delivered" conflicts with edge case 5. Level was made binding (task 010, `1f99872`). Task 010 let stages and share decide when a day was missing; remediations 019 and 020 dropped that fallback (below). Accepted by the owner, 2026-10-19.
- 2026-10-19, owner's decision on dates: dates use the game's existing formatter, "Y17 M5 D3", where the decision gave "Y17 M5" as the example (task 008, `df1f517`). Accepted by the owner, 2026-10-19.
- 2026-10-19, BR-004: the pace warning shows from day 0 of every game. While neither company has started its stage research, the last day to start and keep pace is today. This is BR-004 as written. Playtest 6 will show whether it reads as noise (task 011, `cb953ca`). The owner, 2026-10-19: decide after playtest 6.
- 2026-10-19, BR-001 and BR-005: a player who starts a project outside the program while the rival's lab is free falls behind that day and gets the race stop. This is BR-001 and BR-005 as written. The owner, 2026-10-19: decide after playtest 6.
- 2026-10-19, design: `Notice::foundingDay` and `Notice::rivalFoundingDay` are `std::optional<Day>`, not `Day`, because day 0 is a real day (task 003, `2e8481d`).
- 2026-10-19, BR-012: the end screen's open mines leave out closed mines as well as worked-out ones (task 014, `9689b39`).
- 2026-10-19, BR-008: plot marks are lines of words ("may hold nickel", "survey shows nickel"), not pills. A surveyed plot whose survey shows none of a missing ore still carries "may hold", BR-008 read plainly (task 012, `5844377`).
- 2026-10-19, design: `programPastDeadline` is recomputed each tick rather than made sticky. The two are equivalent, because the earliest founding day never moves earlier (task 004, `6f6a81d`).
- 2026-10-19, design: the layout probe's "Behind by N days" scenario runs on a copy of the content in which the rival delivers nothing, since the shipped game never produces that state at the probe's day (task 010, `1f99872`).
- 2026-10-19, build: a release-only GCC null-dereference warning in `Session::markEnding` was fixed (remediation 017, `6de2775`).
- 2026-10-19, design: the rule for a mine yielding an ore is public in the sim, `sim::mineYieldsOre` in `sim/include/sim/view.h`, so the client's Plots tab marks do not copy it (task 012, `5844377`).
- 2026-10-19, BR-002 and edge case 4: with no solvent rival the verdict is empty, neither Ahead, Behind nor Level. This replaces task 010's stages-and-share fallback, which raced a bankrupt rival (Codex P2; remediations 019, `32d3109`, and 020, `68dc4c6`).
- 2026-10-19, BR-006, BR-007 and edge case 9: a mine yields an ore when it is building, operating or mothballed and either its last operating day produced the ore, or it has never extracted the ore and the owner's best survey shows it. An ore the mine has worked out is no longer a source; lifetime extraction does not count (Codex P1, remediation 019, `32d3109`; Codex re-review P2, remediation 021, `31a05de`).
- 2026-10-19, BR-001: a busy lab is counted after a stage's research is done, since the next stage's project waits for it, but not once the last stage's research is done, since the founding order needs no lab (Codex P1, remediation 019, `32d3109`).
- 2026-10-19, BR-003 and AC-004: the status strip's race item opens the Dashboard and the Dashboard's race strip opens the Program tab. The spec named them only as places to read (Codex P3, remediation 020, `68dc4c6`).

Open after build:
- AC-012: the monitored playtest (playtest 6) has not been played. The spec is not done until it has.
- Two departures wait on playtest 6, by the owner's choice (2026-10-19): the pace warning from day 0, and the race stop when the player starts a project outside the program. Level on equal days and dates as "Y17 M5 D3" are accepted.
- The golden values are unconfirmed on Windows/MSVC until CI runs.
- `kLesserPartnerStages` stays in code, on the do-not-copy list, for spec 010 step 4 (GA-010).
- `status.paused_by`, `ui.race_ahead` and `ui.race_behind` are unused. They are kept in the catalogue.
- The `.meta` descriptions are behind the code: `game/session/include/game/.meta/description.md` still names `takePausedBy`, and `sim/src/.meta/description.md` does not mention `validatePace`, `mineYieldsOre` or `leadingRival`.
- No layout probe pass shows a bankrupt rival (AC-002's evidence, from the spec-enforcer).
- Nothing on screen tells the player the race item and the race strip can be clicked. Whether they need a hover cue is a question for `docs/design/ui-design.md`.

## Open questions
For the architect, in the design:
- Answered by the design (2026-10-19): in the sim, `sim::earliestFoundingDay` in `sim/include/sim/program.h`, carried in the view (ADR 001). Where the earliest founding day is worked out. This draft puts it in the sim, as a function beside `programProgressPermille` that fills the view, so that the client, the AI and the runner read one rule (R-001). It is not a rule the sim enforces, so the client could derive it from the view and the content instead.
- Answered by the design (2026-10-19): per-company flags in saved state, `Company::programBehind` and `programPastDeadline`, set by tick step 9b (ADR 002). How BR-005 remembers that it has already spoken. A per-company flag in saved state is the plain answer, and it moves the golden hash.
