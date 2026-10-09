# 003: Client restyle (Control Room) with the playtest fixes

Status: approved 2026-10-09.

## Goal
The client looks and behaves like the locked design (`docs/design/ui-design.md`,
`docs/design/handoff.md`): the Control Room plate with title blocks, the
persistent status strip, the icon rail, the H4 dashboard, and all five tabs
laid out as on the bench, legible on a Steam Deck. The four remaining
first-batch playtest fixes ride along because they are interface work on
the same screens: auto-pause on notices, locked-ore labelling with a build
warning, the plot table's bid count, closing day and surveyed flag with the
repeat-bid note, and days remaining on mines. We will know it works when the
layout probe passes every tab at both frame sizes, the owner's next playtest
trace shows no insolvency reached without an auto-pause, and no screen
needs explanation.

## Player-facing behaviour
- **Plate.** Canvas, panels, edges, text greys, one blue for actions and
  selection, amber for warnings, red for danger, green for good, exactly as
  `tokens.css`. Fonts: Barlow (UI), Barlow Condensed (display, title
  blocks, nav), JetBrains Mono (every figure). Title blocks (mono index,
  condensed title, 2px rule) on every section. No decoration.
- **Header and strip.** Header: company, date, cash, spendable, debt, a
  90-day cash sparkline, speed controls, Save, Load, New game. Under it the
  status strip, always visible on every tab: month net, storage cost per
  month, solvency chip with grace months, next auction (plot and closing
  day), mine-state chips, research with completion day, world event when
  one is active. The strip wraps at the item boundary and never clips.
- **Rail.** 56px icon rail on the left with the six tabs; expands to 160px
  with labels on hover; the active tab is filled with the accent.
- **Dashboard (H4).** 01 Needs you: numbered rows, each a verb phrase, a
  sub-line, a day count and (where one applies) an action button: auctions
  closing where the player has a bid or a survey (Raise bid), mines
  opening, active events ending, research completing, mines idle with
  nothing extractable (Mothball), warehouse storage above a tenth of cash
  (Sell). 02 Plots and mines table with 03 Prices beside it. 04 Finance as
  one dense row of six stats with sub-lines. 05 Recent log. No warehouse
  table on the dashboard.
- **Plots.** For-sale table: plot, region, area, ask, bids, closes (day),
  survey summary with chips (surveyed, not surveyed with region odds,
  empty, locked ore). Owned table: plot, region, area, mine state chip,
  output per day, remaining (tonnes and days), Mothball/Reopen. Detail
  panel for the selected plot: area, ask, auction with bid count and
  closing day, capex, region odds, survey estimates with value at today's
  prices and a "needs <tech>" label on locked ore, a warning line when a
  rival bid exists ("Contested plots have been clearing 10 to 30% over the
  ask. Bidding again replaces your bid."), a build warning when every
  known deposit is locked or the plot is unsurveyed, bid field and Place
  bid, Build mine, Survey (again).
- **Market.** Per resource: name with demand and supply sub-line, 90-day
  sparkline, price with base and ratio, stock and value, Sell now (amount
  and Sell all), standing order (Auto-sell, Reserve, Floor, Apply) and the
  month-to-date line. 360-day chart for the selected resource. Storage
  panel with the monthly cost.
- **Research.** Laboratory block (one project at a time, busy-until line)
  and the technologies table: name with prerequisites sub-line, status chip
  (done, researching, available, locked), cost, days, effect, Start.
- **Finance.** Finance row, loans table with Repay, Take a loan form with
  terms, Cover costs card, Solvency card.
- **Auto-pause.** The game pauses and shows the notice in the status line
  on: insolvency warning, bankruptcy, auction won or lost, survey complete,
  mine operational, mine found nothing, idle-mine warning. A header control
  opens a small list of toggles; defaults: all on except survey complete
  and mine operational. Choices persist in the per-user data directory.
- **Days remaining on mines.** Estimated from the owner's best survey
  estimate less tonnes extracted, divided by the mine's last output;
  shown on the owned-plots row and the dashboard; a log line a month
  before the estimate runs out.
- **Wording.** "No ore found on plot #N; the mine stands idle." replaces
  "operational" then "depleted" on the same day. Every new string goes
  into `data/strings.json` through the prose pipeline.
- **Steam Deck.** Below 1300px width (or with `--deck`), controls are 40px
  tall, secondary text is 14px, the header shortens Step day and New game,
  the research item of the strip starts a second line.

## Sim rules
- New notice `IdleMineWarning` (private): emitted at each month end for an
  operating mine whose remaining extractable deposits are all empty or
  locked, carrying the mine and plot. No opex change (decision 2026-10-08).
- New notice `MineFoundNothing` (private) when a mine becomes operational
  on a plot with no extractable deposit, instead of `MineOperational`
  followed by `MineDepleted` on the same day; the mine state logic is
  unchanged.
- `CompanyView` gains per-mine `bestEstimateTonnes` (own surveys only,
  most recent per resource, zero when unsurveyed) so the client can show
  remaining tonnes without rules of its own. Nothing else changes in the
  sim; no save-format or hash change beyond the view (views are not
  serialized).

## View
As above: `MineView::bestEstimateTonnes` per resource; the rest already
exists (auction bid counts and close days, survey estimates, loans,
research, events, sell policies).

## Client
- `game/assets/fonts/`: Barlow, Barlow Condensed, JetBrains Mono TTFs with
  their OFL licences; DejaVu removed.
- `tools/design/tokens_to_rcss.py` turns `docs/design/tokens.css` into
  `game/assets/ui/tokens.rcss` (RmlUi has no custom properties); the
  generated file is committed and a test checks it matches the tokens.
- `main.rcss` rewritten from `handoff.md` component by component; `main.rml`
  rebuilt per screen in the hand-off's block order; icons as a TGA sprite
  sheet generated by `tools/design/icons.py` (RmlUi's built-in loader reads
  TGA; no image library), with text fallback.
- `price-chart` element gains a compact sparkline mode (height 20dp, no
  axes, dashed base line) and a 360-day mode.
- `ui_model` gains the needs-you rows, strip fields, plot-table columns,
  survey chips, days-remaining, locked labels and warning flags; all as
  presentation of view data.
- Session gains the auto-pause rule set (which notice kinds pause) and a
  `pausedBy` notice for the status line; App persists the toggles.
- The layout probe runs every tab at 1366x820 and 1280x800 and fails on
  collapse, horizontal overflow, or any text below 14dp in the deck frame.

## Tests
- Sim: idle-mine warning fires only for operating mines with nothing
  extractable and only at month end; mine-found-nothing replaces the
  operational/depleted pair; bestEstimateTonnes follows the latest own
  survey.
- Session: auto-pause pauses on the configured kinds and not others;
  toggles round-trip through their file.
- Client: tokens file matches `tokens.css`; probe assertions per tab and
  frame; data-model bindings for every new field (probe reads them).
- Prose: every new key in the catalogue, parity test, agy pass committed.

## Out of scope
Controller input, Steamworks, save slots (next spec), plot resale, AI tuning,
animations.

## Decisions
- 2026-10-09: playtest fixes ride inside the restyle (owner's choice:
  "restyle first").
- 2026-10-09: idle mines keep full opex and warn monthly (owner's choice).
