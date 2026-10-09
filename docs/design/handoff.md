# Deep Claim interface hand-off

Outcome of the Design Studio session of 2026-10-09. Plate C "Control Room" with D's title blocks; dashboard hierarchy H4 (what needs you first, then what you own, then the money). Source of truth for the mockups: `deep-claim-studio.html` beside this file (the Tokens view in its deck prints the block below).

## Tokens

```css
/* Deep Claim · final tokens · plate C "Control Room" with title blocks · dashboard H4 */
:root {
  /* colour */
  --canvas: #0a0f17; /* window ground */
  --panel: #111826; /* sheets, header, status line */
  --panel-2: #172031; /* inset wells, strip, button fill */
  --edge: #243249; /* hairlines, row rules */
  --edge-strong: #2f4160; /* control borders, title-block rule */
  --text: #ffffff; /* primary */
  --text-2: #c9d4e6; /* secondary, keys */
  --text-3: #8fa0ba; /* muted, labels, sub-lines */
  --accent: #2f8cff; /* primary buttons, selection, sparklines */
  --accent-hover: #4a9cff;
  --accent-text: #ffffff; /* text on accent */
  --warn: #f0b340; /* warnings, warn chips */
  --danger: #ff6e6e; /* rejections, insolvency */
  --good: #5fd98a; /* operating, solvent */
  --selection: #163058; /* selected row and tab */
  /* colour (inverse scheme) */
  --inv-canvas: #f3f5f8;
  --inv-panel: #ffffff;
  --inv-panel-2: #eef1f5;
  --inv-edge: #cfd6e0;
  --inv-edge-strong: #b7c1cf;
  --inv-text: #121820;
  --inv-text-2: #2f3a4a;
  --inv-text-3: #4f5c6d;
  --inv-accent: #1f5fb8;
  --inv-accent-hover: #2a6fcc;
  --inv-accent-text: #ffffff;
  --inv-warn: #8a5a00;
  --inv-danger: #b42626;
  --inv-good: #1b6f35;
  --inv-selection: #dbe7f8;
  /* type */
  --font-ui: "Barlow", system-ui, sans-serif; /* all running text and controls */
  --font-num: "JetBrains Mono", ui-monospace, monospace; /* every figure, date and id (tabular by construction) */
  --font-display: "Barlow Condensed", "Barlow", sans-serif; /* h1, title blocks, company name */
  --fs-12: 12px; /* labels, sub-lines, chips, table headers */
  --fs-13: 13px; /* status line, strip, notes */
  --fs-14: 14px; /* body, buttons, inputs */
  --fs-16: 16px; /* reserved */
  --fs-17: 17px; /* header figures, title blocks, finance stats */
  --fs-20: 20px; /* screen title */
  --lh-12: 16px;
  --lh-13: 18px;
  --lh-14: 20px;
  --lh-16: 22px;
  --lh-17: 22px;
  --lh-20: 24px;
  --weight-regular: 400;
  --weight-medium: 500; /* buttons, values */
  --weight-semibold: 600; /* names, h1 */
  --weight-bold: 700; /* title blocks, chips */
  --figures: font-variant-numeric: tabular-nums; /* on every .num, .figure and td.n; the mono face makes it true in RmlUi */
  /* spacing */
  --s-1: 4px; /* grid unit */
  --s-2: 8px; /* cell padding, inner gaps */
  --s-3: 12px; /* control padding, deck-frame gaps */
  --s-4: 16px; /* pane gutter, sheet padding, section gap */
  --s-5: 20px; /* strip column gap */
  --s-6: 24px; /* reserved for large sections */
  --row: 36px; /* table row height */
  --control: 36px desktop / 40px Steam Deck; /* buttons, inputs, checks */
  --chip: 20px tall / 40px min width (24px on Deck);
  /* radii, borders, shadows */
  --radius: 2px; /* everywhere */
  --border: 1px solid edge; /* sheets, rows */
  --border-strong: 1px solid edge-strong; /* controls, table head */
  --title-rule: 2px solid edge-strong; /* under title blocks */
  --shadow: none;
}
```

## Component inventory

Exact CSS from the bench, live rules only (retired directions excluded). Selectors are scoped to `.game`; `.frame.deck` rules are the handheld overrides.

### Header

Company, date and the three money figures as label/value stats; speed and system buttons on the right. One row; the company name truncates before anything wraps.

```css
.game .sub, .game .subline, .game .stat .label, .game .needs .n, .game .log .d, .game th, .game .pill, .game .chip { line-height: var(--lh-12); }
.game .hdr { display: flex; align-items: center; flex-wrap: nowrap; gap: 16px; padding: 8px 16px; background: var(--panel); border-bottom: 1px solid var(--line); min-height: 60px; flex: 0 0 auto; }
.game .stat { display: flex; flex-direction: column; min-width: 0; }
.game .stat .label { font-size: 12px; color: var(--muted); letter-spacing: 0.02em; }
.game .stat .value { font-size: 17px; font-weight: 600; line-height: var(--lh-17); white-space: nowrap; }
.game .stat.company { flex: 0 1 auto; min-width: 0; }
.game .stat.company .value { font-family: var(--font-display); font-size: 19px; overflow: hidden; text-overflow: ellipsis; max-width: 240px; }
.game .hdr .spacer { flex: 1 1 0; min-width: 8px; }
.frame.deck .game .stat.company .value { max-width: 190px; }
.frame.deck .game .hdr { gap: 12px; }
.game.dir-c .hdr { min-height: 52px; padding: 6px var(--pad); }
.game .speed { display: flex; gap: 4px; }
.game .hdr .sys { display: flex; gap: 4px; }
.game .statusbar { flex: 0 0 auto; min-height: 36px; display: flex; align-items: center; gap: 12px; padding: 0 16px; background: var(--panel); border-top: 1px solid var(--line); color: var(--text-2); font-size: 13px; }
.game .statusbar .lbl { color: var(--muted); text-transform: uppercase; letter-spacing: 0.06em; font-size: 11px; }
.game .statusbar.danger { color: var(--danger); }
```

### Status strip

Dashboard only. Six facts in order (month net, storage, solvency, next auction, mine states, research) plus the World event; wraps at item boundaries.

```css
.game .statusbar, .game .strip, .game .btn.sm, .game .field label, .game .rivalline, .game .needs .t .sub { line-height: var(--lh-13); }
.frame.deck .game .sub, .frame.deck .game .subline, .frame.deck .game .stat .label, .frame.deck .game th, .frame.deck .game .chip, .frame.deck .game .pill, .frame.deck .game .log .d, .frame.deck .game .statusbar, .frame.deck .game .strip, .frame.deck .game .needs .n, .frame.deck .game .needs .t .sub, .frame.deck .game .finrow .stat .sub, .frame.deck .game .notice, .frame.deck .game .field label, .frame.deck .game .rivalline { font-size: 14px; line-height: var(--lh-14); }
.game .strip { display: flex; align-items: center; flex-wrap: wrap; gap: 8px 20px; padding: 4px 16px; min-height: 32px; background: var(--panel-2); border-bottom: 1px solid var(--line); font-size: 13px; color: var(--text-2); flex: 0 0 auto; }
.game .strip > span { white-space: nowrap; }
.game .strip .brk { display: none; }
.frame.deck .game .strip .brk { display: block; flex-basis: 100%; height: 0; }
.game .strip .k { color: var(--muted); margin-right: 6px; }
```

### Rail (icon nav)

56px icon rail; expands to 160px on hover and shows labels; the active tab is filled with the accent.

```css
.frame.deck .game .nav .hint { font-size: 14px; line-height: var(--lh-14); }
.game .nav { flex: 0 0 180px; padding: 16px; display: flex; flex-direction: column; gap: 4px; border-right: 1px solid var(--line); background: var(--canvas); }
.game .nav .tab { display: flex; align-items: center; gap: 10px; height: 44px; padding: 0 12px; border-radius: var(--radius); color: var(--text-2); background: transparent; border: 1px solid transparent; font-weight: 500; text-align: left; }
.game .nav .tab:hover { background: var(--panel); color: var(--text); }
.game .nav .tab.on { background: var(--select); color: var(--text); border-color: var(--line-2); }
.game .nav .hint { margin-top: auto; color: var(--muted); font-size: 12px; line-height: 1.5; }
.game.dir-c .nav { flex: 0 0 56px; padding: 8px 6px; gap: 2px; align-items: stretch; overflow: hidden; }
.game.dir-c .nav:hover { flex-basis: 160px; }
.game.dir-c .nav .tab { padding: 0 11px; justify-content: flex-start; gap: 12px; min-height: 44px; }
.game.dir-c .nav .tab .tl { display: none; font-size: 14px; white-space: nowrap; }
.game.dir-c .nav:hover .tab .tl { display: inline; }
.game.dir-c .nav .tab.on { background: var(--accent); color: var(--accent-text); border-color: var(--accent); }
.game.dir-c .nav .tab svg { width: 22px; height: 22px; flex: 0 0 22px; }
.game.dir-c .nav .hint { display: none; }
```

### Title block

Mono index, condensed uppercase title, 2px rule beneath. Indices restart at 01 per screen.

```css
.game h1 { font-family: var(--font-display); font-size: 20px; font-weight: 600; margin: 0; line-height: var(--lh-20); }
.game.dir-c h1 { font-size: 20px; letter-spacing: 0.02em; }
.game h2 { font-size: 15px; font-weight: 600; margin: 0; color: var(--text); }
.game.dir-c h2 { font-family: var(--font-display); font-size: 17px; font-weight: 700; letter-spacing: 0.06em; text-transform: uppercase; color: var(--text); border-bottom: 2px solid var(--line-2); padding-bottom: 5px; margin-bottom: 2px; display: flex; align-items: baseline; gap: 10px; }
.game.dir-c h2 .idx { font-family: var(--font-num); font-size: 12px; color: var(--muted); letter-spacing: 0; font-weight: 400; }
```

### Panel (sheet, card)

1px edge, 2px radius, 16px padding, 8px internal gap.

```css
.game .sheet, .game .card { background: var(--panel); border: 1px solid var(--line); border-radius: var(--radius); padding: 16px; display: flex; flex-direction: column; gap: 8px; min-width: 0; }
.game .cards { display: flex; gap: var(--gap); flex-wrap: wrap; }
.game .cards .card { flex: 1 1 260px; }
.frame.deck .game .split .side { flex-basis: 320px; }
.game .split { display: flex; gap: var(--gap); flex: 0 0 auto; align-items: flex-start; }
.game .split .main { flex: 1 1 0; min-width: 0; display: flex; flex-direction: column; gap: var(--gap); }
.game .split .side { flex: 0 0 380px; display: flex; flex-direction: column; gap: var(--gap); }
.game.dir-c .split .side { flex-basis: 340px; }
```

### Table and row states

36px rows, 8px cell padding, tabular mono figures in numeric cells; hover and selected fills.

```css
.game .num, .game .figure, .game td.n, .game .val { font-family: var(--font-num); font-variant-numeric: tabular-nums; }
.game table { width: 100%; border-collapse: collapse; }
.game th { text-align: left; font-size: 12px; font-weight: 600; color: var(--muted); letter-spacing: 0.04em; text-transform: uppercase; padding: 8px; border-bottom: 1px solid var(--line-2); }
.game td { padding: 0 8px; height: var(--row); border-bottom: 1px solid var(--line); vertical-align: middle; }
.game td.n { font-family: var(--font-num); font-variant-numeric: tabular-nums; }
.game td .btn, .game td input.text { vertical-align: middle; }
.game tr.sel td { background: var(--select); }
.game td.n, .game th.n { text-align: right; }
.game td.n .num, .game td.n .sub, .game td.num, .game td.nw, .game td.name, .game td.act { white-space: nowrap; }
.game td.wrap { white-space: normal; }
.frame.deck .game th, .frame.deck .game td { padding-left: 8px; padding-right: 8px; }
.game td .sub { display: block; }
.game td.act { text-align: right; white-space: nowrap; }
.game tr.row:hover td { background: var(--panel-2); }
.game tr.sel:hover td { background: var(--select); }
.game .name { font-weight: 600; }
.game .sub { font-size: 12px; color: var(--muted); display: block; }
```

### Chip and pill

State chips (good/warn/danger/muted/accent) and inline pills, 20px tall, 40px minimum width, outline in the state colour.

```css
.game .sub, .game .subline, .game .stat .label, .game .needs .n, .game .log .d, .game th, .game .pill, .game .chip { line-height: var(--lh-12); }
.frame.deck .game .sub, .frame.deck .game .subline, .frame.deck .game .stat .label, .frame.deck .game th, .frame.deck .game .chip, .frame.deck .game .pill, .frame.deck .game .log .d, .frame.deck .game .statusbar, .frame.deck .game .strip, .frame.deck .game .needs .n, .frame.deck .game .needs .t .sub, .frame.deck .game .finrow .stat .sub, .frame.deck .game .notice, .frame.deck .game .field label, .frame.deck .game .rivalline { font-size: 14px; line-height: var(--lh-14); }
.frame.deck .game .chip, .frame.deck .game .pill { height: 24px; }
.game .chip { display: inline-flex; align-items: center; justify-content: center; min-width: 40px; height: 20px; padding: 0 8px; border-radius: var(--radius); font-size: 12px; font-weight: 600; letter-spacing: 0.02em; border: 1px solid currentColor; white-space: nowrap; }
.game .chip.accent { color: var(--accent); }
.game .chip.good { color: var(--good); }
.game .chip.warn { color: var(--warn); }
.game .chip.danger { color: var(--danger); }
.game .chip.muted { color: var(--muted); }
.game .pill { display: inline-flex; align-items: center; justify-content: center; min-width: 40px; height: 20px; font-size: 12px; font-weight: 600; padding: 0 7px; border-radius: var(--radius); border: 1px solid var(--line-2); color: var(--text-2); letter-spacing: 0.02em; white-space: nowrap; }
.game .pill.warn { color: var(--warn); border-color: var(--warn); }
.game .pill.danger { color: var(--danger); border-color: var(--danger); }
.game .pill.good { color: var(--good); border-color: var(--good); }
.game .pill.accent { color: var(--accent); border-color: var(--accent); }
```

### Button

Control height 36px desktop / 40px Steam Deck; primary, active, small, disabled, hover.

```css
.game .statusbar, .game .strip, .game .btn.sm, .game .field label, .game .rivalline, .game .needs .t .sub { line-height: var(--lh-13); }
.game .btn .short { display: none; }
.frame.deck .game .btn { padding: 0 10px; }
.frame.deck .game .btn .long { display: none; }
.frame.deck .game .btn .short { display: inline; }
.game .btn { background: var(--panel-2); color: var(--text); border: 1px solid var(--line-2); border-radius: var(--radius); padding: 0 12px; height: var(--control); font-size: 14px; line-height: var(--lh-14); font-weight: 500; display: inline-flex; align-items: center; gap: 6px; white-space: nowrap; }
.game .btn:hover { border-color: var(--text-2); }
.game .btn.primary { background: var(--accent); border-color: var(--accent); color: var(--accent-text); }
.game .btn.primary:hover { background: var(--accent-hover); }
.game .btn.active { background: var(--accent); border-color: var(--accent); color: var(--accent-text); }
.game .btn.sm { padding: 0 10px; font-size: 14px; }
.game .btn.disabled { opacity: 0.55; cursor: default; }
.game td .btn, .game td input.text { vertical-align: middle; }
```

### Text input and check

Same control height as buttons; mono figures; check box with accent fill when on.

```css
.game .statusbar, .game .strip, .game .btn.sm, .game .field label, .game .rivalline, .game .needs .t .sub { line-height: var(--lh-13); }
.frame.deck .game .sub, .frame.deck .game .subline, .frame.deck .game .stat .label, .frame.deck .game th, .frame.deck .game .chip, .frame.deck .game .pill, .frame.deck .game .log .d, .frame.deck .game .statusbar, .frame.deck .game .strip, .frame.deck .game .needs .n, .frame.deck .game .needs .t .sub, .frame.deck .game .finrow .stat .sub, .frame.deck .game .notice, .frame.deck .game .field label, .frame.deck .game .rivalline { font-size: 14px; line-height: var(--lh-14); }
.game td .btn, .game td input.text { vertical-align: middle; }
.game .field { display: flex; align-items: center; gap: 6px; }
.game .field label { color: var(--text-2); font-size: 13px; }
.game input.text { background: var(--canvas); color: var(--text); border: 1px solid var(--line-2); border-radius: var(--radius); height: var(--control); padding: 0 10px; width: 110px; font-family: var(--font-num); font-variant-numeric: tabular-nums; font-size: 14px; }
.game .check { display: inline-flex; align-items: center; gap: 8px; height: var(--control); }
.game .check i { width: 18px; height: 18px; border: 1px solid var(--line-2); border-radius: 3px; display: inline-block; background: var(--canvas); position: relative; }
.game .check.on i { background: var(--accent); border-color: var(--accent); }
.game .check.on i::after { content: ""; position: absolute; left: 5px; top: 2px; width: 5px; height: 9px; border: solid var(--accent-text); border-width: 0 2px 2px 0; transform: rotate(45deg); }
```

### Key/value row

Label with a 104px minimum, 12px column gap, value right-aligned as one line plus an optional sub-line; stacked variant for names.

```css
.game .kv { display: flex; justify-content: space-between; column-gap: 12px; row-gap: 2px; align-items: baseline; min-height: 20px; }
.game .kv .k { color: var(--text-2); flex: 0 0 auto; min-width: 104px; }
.game .kv .v { font-weight: 500; min-width: 0; text-align: right; flex: 1 1 auto; display: flex; flex-direction: column; align-items: flex-end; gap: 1px; }
.game .kv .v .line { white-space: nowrap; }
.game .kv .v .subline { font-size: 12px; color: var(--muted); font-weight: 400; white-space: nowrap; }
.game .kv .v.wrapok .line { white-space: normal; text-align: right; }
.game .kv.stack { flex-direction: column; align-items: flex-start; gap: 2px; }
.game .kv.stack .v { text-align: left; }
.game .kv.stack .k { white-space: nowrap; font-weight: 600; color: var(--text); min-width: 0; }
.game .kv.stack .v { align-items: flex-start; }
.game .kv.big .v { font-size: 20px; font-weight: 600; }
.game .meter { height: 6px; background: var(--panel-2); border: 1px solid var(--line); border-radius: 3px; overflow: hidden; }
.game .meter i { display: block; height: 100%; background: var(--accent); }
.game .meter.good i { background: var(--good); }
.game .meter.warn i { background: var(--warn); }
```

### Needs-you row

Numbered row: verb phrase with sub-line, right-aligned day count in mono, 110px action column.

```css
.game .sub, .game .subline, .game .stat .label, .game .needs .n, .game .log .d, .game th, .game .pill, .game .chip { line-height: var(--lh-12); }
.game .statusbar, .game .strip, .game .btn.sm, .game .field label, .game .rivalline, .game .needs .t .sub { line-height: var(--lh-13); }
.frame.deck .game .sub, .frame.deck .game .subline, .frame.deck .game .stat .label, .frame.deck .game th, .frame.deck .game .chip, .frame.deck .game .pill, .frame.deck .game .log .d, .frame.deck .game .statusbar, .frame.deck .game .strip, .frame.deck .game .needs .n, .frame.deck .game .needs .t .sub, .frame.deck .game .finrow .stat .sub, .frame.deck .game .notice, .frame.deck .game .field label, .frame.deck .game .rivalline { font-size: 14px; line-height: var(--lh-14); }
.game .needs { display: flex; flex-direction: column; }
.game .needs .row { display: flex; align-items: center; gap: 16px; min-height: 44px; padding: 4px 0; border-top: 1px solid var(--line); }
.game .needs .row:first-child { border-top: 0; }
.game .needs .n { font-family: var(--font-num); color: var(--muted); font-size: 12px; width: 20px; }
.game .needs .t { flex: 1 1 auto; min-width: 0; }
.game .needs .t .sub { font-size: 12px; }
.game .needs .days { font-family: var(--font-num); font-variant-numeric: tabular-nums; white-space: nowrap; color: var(--text-2); width: 92px; text-align: right; }
.game .needs .act { width: 110px; text-align: right; }
```

### Finance stat row

Six stats separated by hairlines; value 17px, optional sub-line.

```css
.game .statusbar, .game .strip, .game .btn.sm, .game .field label, .game .rivalline, .game .needs .t .sub { line-height: var(--lh-13); }
.frame.deck .game .sub, .frame.deck .game .subline, .frame.deck .game .stat .label, .frame.deck .game th, .frame.deck .game .chip, .frame.deck .game .pill, .frame.deck .game .log .d, .frame.deck .game .statusbar, .frame.deck .game .strip, .frame.deck .game .needs .n, .frame.deck .game .needs .t .sub, .frame.deck .game .finrow .stat .sub, .frame.deck .game .notice, .frame.deck .game .field label, .frame.deck .game .rivalline { font-size: 14px; line-height: var(--lh-14); }
.game .finrow { display: flex; gap: 0; }
.game .finrow .stat { flex: 1 1 0; padding: 0 16px; border-left: 1px solid var(--line); min-width: 0; }
.game .finrow .stat:first-child { padding-left: 0; border-left: 0; }
.game .finrow .stat .value { font-size: 17px; line-height: var(--lh-17); }
.game .finrow .stat .sub { font-size: 12px; color: var(--muted); white-space: nowrap; }
.game .rivalline { font-size: 13px; color: var(--text-2); }
```

### Prices panel row

Name with stock sub-line, 90-day sparkline, price with ratio.

```css
.game .prices { display: flex; flex-direction: column; gap: 10px; }
.game .prices .pr { display: flex; align-items: center; gap: 10px; }
.game .prices .pr .nm { flex: 1 1 auto; min-width: 0; font-weight: 600; }
.game .prices .pr .nm .sub { font-weight: 400; }
.game .prices .pr .num { white-space: nowrap; text-align: right; }
.game svg.spark { display: block; }
.game svg.spark .base { stroke: var(--line-2); stroke-width: 1; stroke-dasharray: 3 3; }
.game svg.spark .ln { stroke: var(--accent); stroke-width: 1.5; fill: none; }
.game svg.spark .ar { fill: var(--accent); opacity: 0.12; }
.game svg.spark .pt { fill: var(--accent); }
```

### Log line

Mono date column (96px) and text; warn variant.

```css
.game .sub, .game .subline, .game .stat .label, .game .needs .n, .game .log .d, .game th, .game .pill, .game .chip { line-height: var(--lh-12); }
.frame.deck .game .sub, .frame.deck .game .subline, .frame.deck .game .stat .label, .frame.deck .game th, .frame.deck .game .chip, .frame.deck .game .pill, .frame.deck .game .log .d, .frame.deck .game .statusbar, .frame.deck .game .strip, .frame.deck .game .needs .n, .frame.deck .game .needs .t .sub, .frame.deck .game .finrow .stat .sub, .frame.deck .game .notice, .frame.deck .game .field label, .frame.deck .game .rivalline { font-size: 14px; line-height: var(--lh-14); }
.game .log { display: flex; flex-direction: column; }
.game .log .line { display: flex; gap: 16px; padding: 8px 0; border-top: 1px solid var(--line); }
.game .log .line:first-child { border-top: 0; }
.game .log .d { flex: 0 0 96px; color: var(--muted); font-family: var(--font-num); font-variant-numeric: tabular-nums; font-size: 13px; padding-top: 1px; }
.game .log .t { flex: 1; min-width: 0; }
.game .log .line.warn .t { color: var(--warn); }
```

### Status line

36px bar under the content; danger variant for rejections.

```css
.game .statusbar, .game .strip, .game .btn.sm, .game .field label, .game .rivalline, .game .needs .t .sub { line-height: var(--lh-13); }
.frame.deck .game .sub, .frame.deck .game .subline, .frame.deck .game .stat .label, .frame.deck .game th, .frame.deck .game .chip, .frame.deck .game .pill, .frame.deck .game .log .d, .frame.deck .game .statusbar, .frame.deck .game .strip, .frame.deck .game .needs .n, .frame.deck .game .needs .t .sub, .frame.deck .game .finrow .stat .sub, .frame.deck .game .notice, .frame.deck .game .field label, .frame.deck .game .rivalline { font-size: 14px; line-height: var(--lh-14); }
.game .statusbar { flex: 0 0 auto; min-height: 36px; display: flex; align-items: center; gap: 12px; padding: 0 16px; background: var(--panel); border-top: 1px solid var(--line); color: var(--text-2); font-size: 13px; }
.game .statusbar .lbl { color: var(--muted); text-transform: uppercase; letter-spacing: 0.06em; font-size: 11px; }
.game .statusbar.danger { color: var(--danger); }
```

### Notice and research tiles

Warn-bordered notice with a "!" mark; research tiles with done/now states.

```css
.frame.deck .game .sub, .frame.deck .game .subline, .frame.deck .game .stat .label, .frame.deck .game th, .frame.deck .game .chip, .frame.deck .game .pill, .frame.deck .game .log .d, .frame.deck .game .statusbar, .frame.deck .game .strip, .frame.deck .game .needs .n, .frame.deck .game .needs .t .sub, .frame.deck .game .finrow .stat .sub, .frame.deck .game .notice, .frame.deck .game .field label, .frame.deck .game .rivalline { font-size: 14px; line-height: var(--lh-14); }
.game .research { display: flex; gap: 8px; flex-wrap: wrap; }
.game .research .tile { border: 1px solid var(--line); border-radius: var(--radius); padding: 4px 8px; font-size: 13px; line-height: var(--lh-13); color: var(--text-2); }
.game .research .tile.done { color: var(--good); border-color: var(--good); }
.game .research .tile.now { color: var(--accent); border-color: var(--accent); }
.game .notice { display: flex; gap: 8px; align-items: flex-start; padding: 8px 12px; border: 1px solid var(--warn); border-radius: var(--radius); color: var(--text); font-size: 13px; line-height: var(--lh-13); }
.game .notice .ic { color: var(--warn); font-weight: 700; }
```

### Steam Deck overrides

Applied when the window is the 1280×800 handheld: 40px controls, 14px minimum text, short button labels, tighter side column.

```css
.frame.deck { width: 1280px; height: 800px; }
.frame.deck .game { --control: 40px; }
.frame.deck .game .btn { padding: 0 10px; }
.frame.deck .game .sub, .frame.deck .game .subline, .frame.deck .game .stat .label, .frame.deck .game th, .frame.deck .game .chip, .frame.deck .game .pill, .frame.deck .game .log .d, .frame.deck .game .statusbar, .frame.deck .game .strip, .frame.deck .game .needs .n, .frame.deck .game .needs .t .sub, .frame.deck .game .finrow .stat .sub, .frame.deck .game .notice, .frame.deck .game .field label, .frame.deck .game .rivalline { font-size: 14px; line-height: var(--lh-14); }
.frame.deck .game .chip, .frame.deck .game .pill { height: 24px; }
.frame.deck .game .nav .hint { font-size: 14px; line-height: var(--lh-14); }
.frame.deck .game .btn .long { display: none; }
.frame.deck .game .btn .short { display: inline; }
.frame.deck .game .stat.company .value { max-width: 190px; }
.frame.deck .game .hdr { gap: 12px; }
.frame.deck .game .strip .brk { display: block; flex-basis: 100%; height: 0; }
.frame.deck .game th, .frame.deck .game td { padding-left: 8px; padding-right: 8px; }
.frame.deck .game .split .side { flex-basis: 320px; }
.frame.deck .game .content { padding: 16px; gap: 12px; }
```

## Per-screen block order

Every screen: header (company, date, cash, spendable, debt, cash sparkline, speed controls, Save/Load/New), then the status strip (Dashboard only), then the icon rail on the left and the content pane, then the status line. Section indices restart at 01 on each screen.

- **Dashboard (H4).** 01 Needs you · 02 Plots and mines with 03 Prices beside it (340px panel) · 04 Finance row · 05 Recent (3 lines). No warehouse table; stock lives in Prices and on Market. The status strip carries the World event here.
- **Plots.** 01 For sale (plot, region, area, ask, bids, closes, survey with flag pill) and 02 Owned (plot, region, area, mine state chip, output, remaining, Mothball) in the main column; 03 selected plot (facts, survey block, contested-auction notice, bid field, Place bid, Survey again) and 04 Research tiles in the 380px side column (320px in the Steam Deck frame).
- **Market.** 01 Prices and standing orders (resource with demand/supply sub-line, 90-day sparkline, price with base and ratio, stock with value, Sell now, Standing order: Auto-sell check, Reserve, Floor, Apply, month-to-date line) · 02 360-day chart of the selected resource with 03 Storage beside it.
- **Research.** 01 Laboratory (one project at a time, busy-until line) · 02 Technologies (name with "needs …" sub-line, status chip, cost, days, effect, Start or researching-until).
- **Finance.** 01 Finance row · 02 Loans (principal, remaining, instalment, months left, repay field and button) and 03 Take a loan (amount, Take loan, terms line) in the main column; 04 Cover costs and 05 Solvency in the side column.

## Component states

| Component | States |
| --- | --- |
| Button `.btn` | default, hover (border → text-2), `.primary` (accent fill, hover accent-hover), `.active` (accent fill; speed and tab selection), `.disabled` (55% opacity, arrow cursor, title explains why), `.sm` (same height, 10px padding), focus-visible (2px accent outline) |
| Text input `.text` | default (canvas fill, edge-strong border), focus-visible outline; mono face, tabular figures |
| Check `.check` | off (canvas box), `.on` (accent box, white tick) |
| Chip `.chip` | `.good`, `.warn`, `.danger`, `.muted`, `.accent`; outline in the state colour, text in the same colour, 20px tall, 40px minimum width |
| Pill `.pill` | neutral, `.warn`, `.danger`, `.good`, `.accent`; same geometry as chip, used inline in tables |
| Table row `tr.row` | default, hover (panel-2 fill), `.sel` (selection fill, stays on hover) |
| Rail tab `.tab` | default, hover (panel fill), `.on` (accent fill, white icon); rail expands 56→160px on hover and shows labels |
| Title block `h2` | one state; index + title + 2px rule |
| Status strip item | plain figure; `.chip` for states; wraps at item boundaries, Research forced to the second line on Deck |
| Status line `.statusbar` | default (text-2), `.danger` (rejection text in danger) |
| Needs-you row | with action button, or without (day count only) |
| Finance stat | value only, or value with sub-line; `.good` value for positive month |
| Prices row | normal, `locked` pill on the name, `warn` ratio when price is below 0.7 of base |
| Log line | default, `.warn` (text in warn) |
| Notice `.notice` | one state; warn border, "!" mark, 13px text |
| Research tile `.tile` | plain, `.done` (good), `.now` (accent) |

## RmlUi translation notes

The mockups use only flexbox and tables, which RmlUi 6 lays out, but several CSS conveniences used here have no RCSS equivalent. Use these fallbacks when writing `main.rcss`.

| Used in the mockup | RmlUi 6 | Fallback |
| --- | --- | --- |
| CSS custom properties (`--token`) | no | Generate `main.rcss` from `data/ui/tokens.json` at build time (the same tool can emit this token block); colours and sizes are literal in RCSS. |
| `gap` on flex rows and columns | no | Margins on children: `margin-right` for row gaps, `margin-bottom` for column gaps, with `:last-child` margin 0 where the gap must not trail. |
| `min-width: 0` on flex children | partial | Give text-bearing flex children an explicit `width`/`flex-basis` and `overflow: hidden`; avoid relying on shrink-to-wrap. |
| `white-space: nowrap`, `text-overflow: ellipsis` | nowrap yes; ellipsis no | Keep nowrap; truncate the company name in code (`formatCompanyName`) to a fixed length instead of CSS ellipsis. |
| `font-variant-numeric: tabular-nums` | no | Use the mono face (JetBrains Mono) for every figure, as the mockup already does; no proportional digits anywhere. |
| `font-variant: small-caps` | no | Not used in the final plate. |
| `:hover`, `:active`, `:focus` | yes (`:hover`, `:active`, `:focus`) | As is; `:focus-visible` becomes `:focus`. |
| `box-shadow`, `backdrop-filter` | box-shadow yes (6.x), backdrop no | Shadows are none in this design; nothing to translate. |
| `inset` box-shadow for selected rows | partial | Use a 1px top/bottom border on `tr.sel td` instead. |
| SVG sparklines inline | no inline SVG | Keep the custom `<price-chart>` element and add a compact mode (120×22 / 90×26) that draws the same line, area and endpoint from the view's price history; the dashed base line is a second series. |
| SVG rail icons | RmlUi loads SVG via the SVG plugin, or use a sprite sheet | Ship the six icons as a 22px PNG/TGA sprite (`decorator: image`) with white and accent variants, or enable RmlUi's SVG plugin (adds lunasvg). Decision for the spec. |
| `transform: scale` (bench only) | n/a | Bench-only; not shipped. |
| `100dvh`, sticky headers (bench only) | n/a | Bench-only. |
| `display: inline-flex` for chips | `inline-block` with fixed height | `display: inline-block; height: 20dp; line-height: 20dp; min-width: 40dp; text-align: center`. |
| `dp` units | yes | Replace every `px` with `dp` so the Steam Deck scale factor applies; the Deck overrides (40dp controls, 14dp minimum text) become a second RCSS block selected by the window's dp ratio. |
| Google Fonts | no | The three faces are OFL; add Barlow, Barlow Condensed and JetBrains Mono TTFs to `game/assets/fonts/` beside DejaVu and load them at startup. |
