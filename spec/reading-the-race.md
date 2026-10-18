# PRD: Reading the race (spec 011, the follow-up to spec 010 step 1)

Status: approved by the owner, 2026-10-18. Not yet designed or built.

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

## Open questions
For the architect, in the design:
- Where the earliest founding day is worked out. This draft puts it in the sim, as a function beside `programProgressPermille` that fills the view, so that the client, the AI and the runner read one rule (R-001). It is not a rule the sim enforces, so the client could derive it from the view and the content instead.
- How BR-005 remembers that it has already spoken. A per-company flag in saved state is the plain answer, and it moves the golden hash.
