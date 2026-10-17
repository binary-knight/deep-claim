# Deep Claim interface design

Status: **locked 2026-10-09** after four Design Studio sprints. Code follows
this document and `handoff.md`; changes go through a new sprint, not ad hoc.

## Intake (2026-10-09)

- **What the interface must do first**: tell the player their next move at a
  glance (command), look like a shipped Steam title (polish), carry the
  trade in its materials (atmosphere), and stay frictionless at 20x
  (speed). Command leads when they conflict.
- **Medium**: desktop window 1366x820 now; Steam Deck 1280x800 on a
  7-inch panel is a hard legibility constraint: body text no smaller than
  14px, buttons at least 40px tall, no horizontal scroll.
- **Voice**: in-world flavour, an operator's desk: a foreman's note, a radio
  call from the survey crew, a month-end line from the books. Numbers
  stay exact and plain. All copy goes through the agy polish pipeline
  (`docs/process.md`).
- **Kept from the current client**: blue-black canvas, white lettering.
- **Playtest lessons the layout must answer** (`docs/playtests/2026-10-08.md`):
  warehouse value and its monthly storage cost next to cash; a visible
  insolvency clock; survey results that say what is extractable and worth
  what; bid count, closing day and surveyed flag in the plot table; the
  auto-sell controls; days remaining on mines; locked ore marked where it
  appears.

## Anti-slop register (banned)

Floating glass cards, decorative gradients, pulsing status dots,
pseudo-terminal `// STATUS` tags, coloured left accent bars as a stand-in
for hierarchy, stock photography or clay illustration. Hierarchy comes
from type size and weight, spacing and alignment.

## Sprint 1: material plate

Three polarised directions were built on the bench
(`deep-claim-studio.html`, published as an artifact) with identical data
and copy: A "Drill Core" (clarity first), B "Field Office" (atmosphere
first), C "Control Room" (speed first).

Round 1 outcome:
- **A rejected: too generic.** Inter and a blue accent read as an admin
  template; nothing of the trade. Codified: `[Constraint: the interface
  must carry a sense of place without pictures]`.
- **C's status strip is kept** regardless of the winning direction: month
  net, storage cost, solvency clock, next auction close, mine states and
  research, always visible under the header. Codified: `[Constraint: the
  facts that ended games are never more than zero clicks away]`.
- B and C go to round 2, with a new D "Core Shack" (B's world with more
  mass and less decoration) as the third candidate.

Round 2 outcome:
- **B rejected: not enough mass.** Next to D its cards read thin and its
  amber is spread too wide. Codified: `[Constraint: weight comes from
  structure (title blocks, drawing borders, mono figures), not from
  colour; accent colour is rationed to a mark or two]`.
- C and D go to the final plate round, head to head on the working
  screens (Plots, Market) at 1:1.

Final: **C "Control Room" is the material world**, with D's title blocks
borrowed as its mass. D read as handsome but still; C earned it on the
working screens, where density is a relief at 20x.

### Material voice: Control Room

- *Wants*: a persistent instrument strip; a rail of icons that stays out
  of the way; state as words in chips (good, warn, danger), never as
  dots; small sparklines where a trend changes a decision; figures in
  mono, aligned; title blocks (index, title, 2px rule) on every section
  so the eye finds the edges; cool blue for action and selection, amber
  for warnings, red for danger only.
- *Refuses*: decoration of any kind, pictures, gradients, glass; colour
  without a meaning; more than one accent on a panel; wrapped figures;
  anything the player must hover to learn.

## Sprint 2: information hierarchy

Plate locked. Three hierarchies of the dashboard on the C plate, same
data, same copy: H1 "Decision desk" (what needs you first), H2 "Ledger"
(the current card layout: company, finance, competitors, world,
warehouse, log), H3 "Operations board" (assets first: plots and mines
with output and remaining, prices beside, log last).

Round 1 outcome:
- **H2 rejected: "cards make me hunt."** Four equal cards give no order of
  importance. Codified: `[Constraint: the dashboard is ordered by what
  needs a decision, never by category]`.
- H1 and H3 go to round 2 with H4 "Decision desk over the mine board":
  needs-you list, then plots and mines with prices beside, then finance,
  then log.

Final: **H4 is the dashboard order**: 01 Needs you (numbered rows: verb
phrase, sub-line, day count, action button), 02 Plots and mines with
03 Prices beside it, 04 Finance as one dense row of six stats with
sub-lines, 05 Recent. Warehouse detail lives on the Market tab. Plots and
Market keep the Control Room layouts from the bench.

## Sprint 3: phrasing

Folded into the prose catalogue (spec 002): every string on the bench is
a draft that goes through `tools/prose/polish.sh`; the design does not
fix wording, it fixes where words sit and how many.

## Sprint 4: spatial calibration

Done: all five screens (Dashboard, Plots, Market, Research, Finance) on a
4px grid with explicit line-heights so labels and figures share baselines;
controls 36px tall on desktop and 40px in the Steam Deck frame, where every
secondary text role is 14px; chips 20px tall with a 40px minimum width;
16px gutters; inverse-scheme tokens darkened to clear 4.5:1. The bench
source is `bench.html`.

## Tokens, components, states

The locked token block is `tokens.css` (colour, type scale 12/13/14/16/17/20
with line-heights, spacing 4/8/12/16/20/24, 2px radii, 1px edges, 2px title
rules, no shadows). The component inventory with exact CSS, states, the
per-screen block order and the RmlUi translation notes is `handoff.md`.
The client restyle (spec 003) translated them into RCSS. The scale is the
rule (R-004 in `ARCHITECTURE.md`); an off-scale value in `main.rcss` is a
violation to tidy when that code is touched.

## The race on screen (spec 010, 2026-10-11)

Spec 010 added a seventh screen and two race displays, built in the
Control Room material. Detail is in `docs/DESIGN.md`, "The race for the
program (spec 010, step 1)".

- **Rail.** Seven icons: Dashboard, Program, Plots, Market, Research,
  Finance, Log. Program is second, after Dashboard.
- **Race item.** On every tab, the status strip under the header shows
  the time left to the deadline in years and days, or who founded the
  program, and a chip that says Ahead, Behind or Level.
- **Race strip.** The Dashboard opens with the race above 01 Needs you:
  the same deadline line and verdict chip, then a row per company with
  stages complete, a progress bar with its delivered percentage beside it,
  and the state of its current stage's research. Who leads is a word, so
  nothing rests on colour.
- **Program tab.** A short intro and, when cover-costs is on, a warning.
  01 Stages lists the three stages, each with a state chip. 02 is the
  current stage: a table of its goods (needed, delivered, in stock, still
  needed) with a Deliver button per good, the stage's cost and research,
  then Start research and Complete stage. Lines under the buttons say
  what the stage still lacks, or that it is ready. An order waiting for
  the clock stands in for its button as a chip with Cancel.
- **End screen.** When the game is over, an overlay covers play, header
  included, so no order control can be reached. It gives the ending in
  its title and body (founded, lesser partner, lost, deadline passed or
  bankrupt), the day the game ended, each company's stages with the day
  of each, the player's cash, mines and stages, and one button, New game.
  The speed buttons are off.

## Inevitability gate

Passed 2026-10-09: the owner reviewed all five screens and locked the
design.
