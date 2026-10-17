# prose

**Purpose:** The three-step authoring pipeline for the player-facing prose catalogue `data/strings.json` (spec 002): a plain draft in TSV, a rewrite pass through the external `agy` LLM CLI guarded by an invariant check, and a conversion to sorted JSON. The intermediate TSV files are committed so the difference between draft and shipped text can be reviewed.

## Key Components
- `README.md` (19 lines) - Describes the Draft / Polish / Convert steps, the commands for each, and the note that one `agy` call handles about 25 lines (`split -l 25 -d draft.tsv part-`).
- `voice.md` (8 lines) - The voice brief (one paragraph of plain text) that `polish.sh` inlines into the prompt under a `VOICE` heading.
- `polish.sh` (42 lines, executable) - `polish.sh <draft.tsv> > <polished.tsv>`. Builds one prompt from fixed instructions, `voice.md` and the whole input file, calls `agy -p "$prompt" --output-format text --log-file "${TMPDIR:-/tmp}/agy-polish.log"`, keeps only reply lines shaped `key<TAB>text`, then walks the input line by line and prints either the polished text or the draft. The draft is kept, with a message on stderr, when the key is missing from the reply (`MISSING`), when the key ends in `.name` (`NAME`, names are never rewritten), or when the `invariants()` set differs (`CHANGED`).
- `to_json.py` (41 lines) - `to_json.py <polished.tsv> <strings.json>`. Reads `key<TAB>text` lines, skips blank lines, and writes a flat JSON object with keys sorted, 2-space indent, `ensure_ascii=False` and a trailing newline. Exit codes: 2 on wrong argument count, 1 on a line without a tab or a duplicate key, 0 on success.
- `draft.tsv` - 416 rows, 2 tab-separated columns, no header row: column 1 is the string key, column 2 is the plain draft text with `{placeholder}` tokens. Key prefixes: `ui.` 266, `notice.` 48, `reject.` 35, `status.` 33, `tech.` 26, `event.` 8.
- `polished.tsv` - 416 rows, same 2 columns, same keys in the same order as `draft.tsv`; column 2 is the text that ships. Input to `to_json.py`.
- `agy-raw.tsv` - 200 rows, same 2 columns; unmodified `agy` output kept for reference. It was last changed in spec 006: 199 of its 200 keys are still in `draft.tsv`; the exception, `notice.mine_switched`, was removed from the draft when spec 008 split that notice into `notice.mine_mothballed` and `notice.mine_reopened`. Not read by any script or test.

## Dependencies
- `polish.sh`: bash, the external `agy` CLI (an LLM command-line client; the README states it needs its Google hosts reachable, i.e. network access), GNU `grep` (uses `-P` and `-oE`), `awk`, `sort`, `tr`, `cat`, `dirname`, `printf`. Reads `voice.md` from its own directory.
- `to_json.py`: Python 3.9 or later (uses `list[str]` in a signature without `from __future__ import annotations`), standard library only (`json`, `sys`). No third-party imports.
- Output consumed elsewhere: `data/strings.json` is loaded by `content/src/loader.cpp` (`kStringsFile` in `content/include/content/loader.h`) and read directly by `tests/test_strings.h`.
- Incoming: `tests/strings_test.cpp` (Catch2, part of `sim_tests`) opens `tools/prose/draft.tsv` via `DC_SOURCE_DIR` in the test case "The draft and the shipped catalogue agree on keys and placeholders". Key scheme and allowed placeholders are defined in `docs/specs/002-prose-catalogue.md`; the pipeline is also described in `docs/DESIGN.md` and `docs/process.md`.
- No `CMakeLists.txt` and no step in `.github/workflows/ci.yml` runs `polish.sh` or `to_json.py`. CI runs `ctest`, which runs `sim_tests` (built when `DC_BUILD_TESTS=ON`, the default), so the draft/catalogue parity check runs in CI.

## Patterns
- Shell (`polish.sh`): `set -euo pipefail`, positional argument with `${1:?usage}`, prompt assembled with command substitution, result on stdout and diagnostics on stderr, fail-safe fallback to the draft line for every rejected rewrite.
- Python (`to_json.py`): single-file CLI, module docstring used as the usage text, `main(argv) -> int` with `sys.exit(main(sys.argv))`, deterministic output (sorted keys, fixed indent) so the JSON diffs cleanly.
- TSV data (`draft.tsv`, `polished.tsv`, `agy-raw.tsv`): headerless, exactly two tab-separated fields per line, one string per line, ASCII, no quoting or escaping; keys are dotted identifiers matching `[A-Za-z0-9_.-]+`.
- Markdown (`README.md`, `voice.md`): prose documentation; `voice.md` is also a runtime input to `polish.sh`.
- Human-in-the-loop generation: `draft.tsv` is the source of truth for meaning, the LLM changes wording only, and the committed `polished.tsv` is the reviewed result.

## Constraints
- `draft.tsv` and `data/strings.json` must have the same key set, the same placeholders per key and no empty shipped text; `tests/strings_test.cpp` enforces this.
- The `invariants()` check in `polish.sh` requires these to survive a rewrite unchanged (compared as a sorted list): `{placeholder}` tokens matching `\{[a-z_]+\}`, plot numbers `#N`, and every figure with its sign, `$` prefix, `k`/`M`/`B`/`%` suffix and following unit (`/t`, ` t`, ` days`, ` months`, ` a year`).
- Keys ending in `.name` always keep the draft text.
- `to_json.py` rejects duplicate keys and lines without a tab; text may itself contain tabs (split is on the first tab only).
- A key or text cannot contain a newline; the formats are strictly line-oriented.
- `polish.sh` sends the entire input in a single `agy` call; the README gives about 25 lines as the working batch size, so larger drafts are split by hand.
- `agy` stderr is discarded (`2>/dev/null`). If `agy` exits nonzero or is not on `PATH`, `set -e` with `pipefail` aborts the script with no output; if `agy` succeeds but returns no `key<TAB>text` lines, every line is reported `MISSING` and passed through as the draft.
- The usage comment in `polish.sh` names the files `draft.txt` / `polished.txt`; the README and the committed files use `.tsv`.
