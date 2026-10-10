# 001-prose

**Purpose:** Archived record of the one-time prose polish pass made for spec 001 (standing sell policies): the plain draft of that spec's 15 new player-facing strings and the agy-polished version of the same 15. It is evidence for the last entry in the Decisions list of `docs/specs/001-sell-policies.md`, not part of the live prose pipeline.

## Key Components
- `draft.tsv` - 15 lines, `key<TAB>text`. Plain drafts of the spec 001 strings. Keys: `notice.cover_costs_sale`, `notice.auto_sale_summary`, `ui.policy_summary_on`, `ui.policy_summary_off`, `ui.cover_on`, `ui.cover_off`, `ui.storage_warning`, `ui.auto_sold`, `status.reserve_prompt`, `status.floor_prompt`, `status.policy_applies`, `status.policy_stops`, `status.cover_on`, `status.cover_off`, `ui.policy_hint`.
- `polished.tsv` - 15 lines, same keys in the same order, with the text as rewritten by the agy CLI. Named placeholders such as `{tonnes}`, `{resource}`, `{amount}`, `{reserve}`, `{price}` appear in both files.

## Dependencies
- Produced with `tools/prose/polish.sh` (agy CLI), as stated in `docs/specs/001-sell-policies.md` (Decisions, 2026-10-09).
- Referenced only by `docs/specs/001-sell-policies.md` line 184. No code, test, CMake file, CI workflow or tool reads these two files.
- The live pipeline files are elsewhere: `tools/prose/draft.tsv` and `tools/prose/polished.tsv` feed `tools/prose/to_json.py`, which writes `data/strings.json`; `tests/strings_test.cpp` reads `tools/prose/draft.tsv`, not this directory.

## Patterns
- Tab-separated `key<TAB>text` lines, one string per line, no header row; the same format as `tools/prose/draft.tsv`.
- Draft and polished files kept side by side so the two can be diffed for surviving numbers and placeholders (the invariant described under "Prose: drafted here, polished by agy" in `docs/process.md`).

## Constraints
- Snapshot, not a source of truth: 14 of the 15 keys are present in `data/strings.json`; `status.policy_applies` is not.
- Spec 001 predates the prose catalogue (spec 002); later specs have no equivalent per-spec prose directory.
- `docs/process.md` rule that applies to the content: numbers, units, plot and resource names and `{placeholders}` must be identical between draft and polished text.
