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

## Tokens, components, states

To be filled when the direction is locked (Sprint 1), then the hierarchy
(Sprint 2), phrasing (Sprint 3) and spacing baselines (Sprint 4).
