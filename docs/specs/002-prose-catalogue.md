# 002: Prose catalogue

Status: approved 2026-10-09; builds after spec 001 lands (both touch format.cpp).

## Goal
Every sentence a player reads lives in one data file, is drafted plainly,
polished by the agy CLI into natural human writing in the game's voice,
and verified unchanged in its numbers and placeholders before it ships.
We will know it works when no player-facing string is a literal in C++
or RML, and `tools/prose/polish.sh` can regenerate the whole catalogue.

## Player-facing behaviour
The player sees the same information as today, written as a foreman's
note or a month-end ledger line rather than a status-code sentence:
"Survey crew finished on plot #4. Estimate is ~366,168 t of iron ore
(error x0.50)." instead of "Survey of plot #4 complete". Numbers, units
and ids never change between the plain draft and the polished text.

## Sim rules
None. The sim emits structured notices (kind, ids, amounts) and knows no
text. The catalogue is a client-side concern loaded by the content
library next to the other data files.

## Data
`data/strings.json`: a flat object of `key: template`. Templates use
named placeholders `{amount}`, `{plot}`, `{company}`, `{resource}`,
`{tonnes}`, `{days}`, `{tech}`, `{event}`, `{loan}`, `{months}`,
`{price}`, `{reason}`. Keys are grouped by prefix: `notice.*` (one per
NoticeKind, with variants where the text depends on state, e.g.
`notice.survey_complete.empty`), `reject.*` (one per RejectReason),
`status.*` (client status lines), `ui.*` (labels, hints, the how-to-play
card), `event.*` and `tech.*` descriptions move here from their data
files as `event.<id>.name` and `event.<id>.description`.

Validation (content loader): every NoticeKind and RejectReason has a key;
every placeholder used by a template is one the code supplies for that
key (a table in the loader); no unknown keys. Missing or malformed
strings are a data error at load, like any other content error.

## Client
`game/session/format.cpp` becomes a renderer of templates: a small
`fill(template, args)` with named substitution, locale-free, unit tested.
`describeNotice`, `rejectReasonText` and the App's status lines look up
keys. RML text that is prose (the how-to card, hints) binds to catalogue
strings through the data model instead of being literal in `main.rml`.
The sim_runner keeps its own terse CSV/JSON output and does not use the
catalogue.

## Pipeline
`tools/prose/draft.tsv` holds the plain drafts (one `key<TAB>text` line
each). `tools/prose/polish.sh draft.tsv > polished.tsv` runs agy with
`tools/prose/voice.md`; a second script `tools/prose/to_json.py` turns
the polished file into `data/strings.json`. The draft is the source of
truth for meaning; the polished file is reviewed by diff before it is
converted. Both are committed so a reviewer can see what agy changed.

## Tests
- Every NoticeKind and RejectReason resolves to a non-empty string from
  the real data (data_files_test).
- `fill` substitutes all placeholders, leaves unknown ones visible as
  `{name}` and never throws.
- The polished catalogue contains the same placeholders per key as the
  draft (a test over the two TSV files).
- Existing format tests are rewritten against the catalogue.

## Out of scope
Localisation (one language), rich text markup inside strings, AI
company names, the sim_runner's output.

## Decisions
- 2026-10-09: agy may rewrite freely; numbers, units, ids and
  placeholders are invariant and checked mechanically.
- 2026-10-09: prose lives in `data/strings.json`, not in code, so the
  pipeline runs on a file and a writer can edit without a compiler.

## Plan (2026-10-09)

Files: `data/strings.json` (new), `data/events.json` and `data/techs.json`
(names removed, ids kept), `sim/include/sim/content.h` (`Content::strings`,
`TechDef::description`, `EventDef::description`), `content/src/strings.cpp`
(new: parse, validate, apply names) and `loader.{h,cpp}` (sixth source),
`game/session/include/game/format.h` and `src/format.cpp` (`fill`, `text`,
catalogue-driven `describeNotice` and `rejectReasonText`), `game/src/app.cpp`
(`status.*`), `game/src/ui_model.{h,cpp}` and `game/assets/ui/main.rml`
(`ui.*` bound through the data model), `tools/prose/draft.tsv`,
`tools/prose/polished.tsv`, `tools/prose/to_json.py`, `tools/prose/README.md`,
tests (`strings_test.cpp` new; `data_files_test.cpp`, `content_test.cpp`,
`session_test.cpp` updated).

Key scheme: `notice.<kind>` with `.plot`, `.plot_amount`, `.sale`,
`.research`, `.loan`, `.loan_amount`, `.amount` variants for rejections;
`reject.<reason>`; `status.<name>` for every status line the client sets;
`ui.<name>` for prose in the model and the RML, with `.one` and `.many`
variants where English pluralises; `event.<id>.name|description` and
`tech.<id>.name|description`. Placeholders allowed per key are a static
table in the loader; every key in the table is required and no other key
is accepted.

Strings are not part of the content fingerprint: prose does not change
simulation state, and a wording fix must not invalidate saves. Tech and
event names stay in their defs (and therefore in the fingerprint, as
before), filled from the catalogue by the loader.
