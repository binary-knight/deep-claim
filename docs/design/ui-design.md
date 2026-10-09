# Deep Claim interface design

Status: in progress (Design Studio session, 2026-10-09). Code follows this
document; the document is updated at the end of each sprint.

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

## Tokens, components, states

To be filled when the direction is locked (Sprint 1), then the hierarchy
(Sprint 2), phrasing (Sprint 3) and spacing baselines (Sprint 4).
