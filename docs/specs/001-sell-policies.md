# Spec 001: Standing sell policies

Status: approved 2026-10-09 (plan reviewed by the player after playtest 1).
Source: `docs/playtests/2026-10-08.md`, finding 1.

## Goal

Stop unsold ore from being the dominant way to lose. Every game in the
first playtest hit the insolvency warning while the warehouse held ore
worth several times the shortfall, because selling is a manual order and
storage is charged every month. A company can set a standing policy per
resource that the sim applies every day, and a month-end safety rule that
sells just enough to stay solvent.

## Player-facing behaviour

- Market tab, one row per resource: an **Auto-sell** checkbox, a
  **Reserve** field (tonnes to keep), a **Floor** field (minimum price per
  tonne), an **Apply** button, and a line "sold this month: 1,850 t for
  $1.6M".
- Finance tab: a **Cover costs at month end** checkbox.
- Dashboard finance card: when the monthly storage cost exceeds a tenth of
  cash, a warning line naming the cost and pointing at the Market tab.
- New games start with auto-sell on for every resource (reserve 0, floor 0)
  and cover costs on. The four modes the player chose map onto these
  controls: sell everything (on, 0, 0); keep a reserve (on, N, 0); sell
  above a floor (on, 0, price); cover costs (the company flag).
- Manual sell orders keep working alongside.
- The log gets one summary line per resource per month instead of a line
  per automatic sale. A cover-costs sale is logged the day it happens.

## Sim rules

### State

`Company` gains:

- `std::vector<SellPolicy> sellPolicies` indexed by `ResourceId`, where
  `SellPolicy { uint8 enabled; int64 reserveTonnes; Money floorPrice; }`.
- `uint8 coverCosts`.
- `std::vector<int64> autoSoldTonnes` and `std::vector<Money> autoSoldProceeds`,
  indexed by `ResourceId`: month-to-date totals of automatic sales, reset
  after the month-end summary.

All of it is serialized, hashed and validated on load (vector sizes equal
the resource count, flags are 0 or 1, reserve and floor non-negative and
within the stock and cash bounds, month-to-date totals non-negative).

### Commands

- `SetSellPolicy { company, resource, enabled, reserveTonnes, floorPrice }`.
  Rejected with `UnknownCompany`, `CompanyBankrupt`, `UnknownResource`,
  `InvalidQuantity` (reserve below zero or above `kMaxStockTonnes`) or
  `InvalidAmount` (floor below zero or above `kMaxCashCents`). A policy on
  a locked resource is accepted and never fires.
- `SetCoverCosts { company, enabled }`. Rejected with `UnknownCompany` or
  `CompanyBankrupt`.

Both are ordinary commands: queued, applied at the next tick in company
order, recorded in the playtest trace and replayed like any other.

### Daily step

A new pipeline stage, `runSellPolicies`, runs after mines and before
markets (so the day's output sells at today's price, exactly as a manual
order submitted today would). For each solvent company in id order and
each resource in id order with an enabled policy: if today's price is at
or above the floor, sell `stock - reserve` tonnes when that is positive.
The sale goes through the same path as a manual sale (`applySell`), so
supply smoothing, the cash cap and the daily supply saturation behave
identically; the tonnes are first clamped to what the cash cap allows.
No notice is emitted; the month-to-date totals are updated instead.

### Month end

In `runFinance`, after storage and loan charges and before the solvency
check:

1. **Cover costs.** If the flag is set and cash is below zero, sell stock
   from the resource with the largest stock value first (ties by lower
   resource id), as many tonnes as needed to bring cash to zero or more
   (rounded up), then move to the next resource. Emits a
   `CoverCostsSale` notice per resource sold (amount = proceeds, tonnes).
   Locked resources are skipped. If stock runs out the company stays
   insolvent and the normal warning follows.
2. **Summary.** For each resource with a non-zero month-to-date total,
   emit `AutoSaleSummary` (amount = proceeds, tonnes) and reset the
   totals. Cover-costs sales are counted in the totals too.

### Defaults

`balance.json` gains:

```json
"default_sell_policy": { "enabled": true, "reserve_tonnes": 0, "floor_price": 0 },
"default_cover_costs": true
```

Applied to the player company at world generation; AI companies start
with policies off and no cover-costs rule, because the AI keeps its own
selling rule this step (see out of scope). Reasoning for the default:
three games out of three were lost or nearly lost to unsold ore; the
convenience should be on unless the player chooses to speculate.

### Notices

- `CoverCostsSale` (private): company, resource, tonnes, amount.
- `AutoSaleSummary` (private): company, resource, tonnes, amount.

## View

`CompanyView` gains `sellPolicies`, `coverCosts`, `autoSoldTonnes` and
`autoSoldProceeds` (own company only).

## Client

- Market tab: each resource row shows its policy ("Auto-sell, keep 500 t,
  at $850/t or more" or "Manual") and the month-to-date line. One panel
  below the table edits the policy of the selected resource: reserve and
  floor inputs, Apply, and Turn on / Turn off. Apply sends `SetSellPolicy`
  with the parsed inputs (strict money and tonnes parsers already exist);
  invalid text is reported in the status line without sending anything.
  The inputs are prefilled from the sim on a change of selection, on a
  new game and on load, and never while the player is editing.
- Finance tab: a Cover costs card with the current state explained and a
  Turn on / Turn off button that sends `SetCoverCosts`.
- Dashboard finance card and Finance tab warning when there is a storage
  cost and either cash is at or below zero or the monthly storage cost
  exceeds a tenth of cash. This is presentation, not a rule.
- Log text for the two new notices and the new reject paths.

## Tests

- Sell everything, keep a reserve, floor price (both sides of it), locked
  resource never sells, disabled policy never sells.
- Cover costs: brings cash to zero or above, sells the most valuable stock
  first, emits the notice, does nothing when the flag is off or cash is
  positive, runs out of stock gracefully.
- Month-end summary notice and reset.
- Command validation and reject reasons.
- Save round trip with policies set; load rejects bad sizes and flags.
- Balance keys load from JSON with validation errors for bad values.
- Trace round trip of both commands.
- View exposes the fields.
- Golden determinism hash updated once.

## Out of scope

- The AI keeps its own daily selling rule (85% of base). Switching it to
  policies belongs with the AI tuning step.
- Plot resale.
- Per-mine policies: policies are per resource; a company sells from one
  warehouse.

## Decisions

- Default on for new games (from `balance.json`).
- Monthly summary notices instead of a notice per automatic sale, to keep
  the 500-entry log readable; cover-costs sales are logged immediately.
- One struct per resource `{enabled, reserveTonnes, floorPrice}` plus a
  company `coverCosts` flag; the four player-chosen modes are
  combinations of these.
- Daily sales run after mines and before markets, matching when a manual
  order submitted the same day would execute.
- The selected-resource panel with buttons replaced the per-row checkbox
  controls first sketched: two-way inputs inside an RmlUi `data-for` row
  cannot be verified without a display, and one panel keeps the table
  readable. Buttons rather than checkboxes because a checkbox's change
  event and its data binding order is not something the headless probe
  can check.
- Month-to-date totals saturate at the state bounds (stock bound for
  tonnes, cash bound for proceeds). Each addend is within the same bound,
  so the addition cannot overflow; only a month of absurd turnover would
  hit the cap. (Codex review flagged the saturation; accepted as is.)
- Cover-costs ordering compares price times stock exactly with 128-bit
  intermediates (Codex review: the earlier overflow fallback made large
  stocks tie).
- A cover-costs sale happens after the market stage has consumed and reset
  the day's supply counter, so it does not register as supply (Codex
  review finding: counting it left a state that failed save validation).
  Month-end sales are rare and small, so the price effect is negligible.
