# Prose pipeline

Every sentence a player reads lives in `data/strings.json` (spec 002). It is
produced in three steps, and all three files are committed so a reviewer can
see what changed between the plain draft and the shipped text.

1. **Draft** `draft.tsv`: one `key<TAB>text` line per string, written plainly
   with every number, unit, id and `{placeholder}` in place. This file is the
   source of truth for meaning. Keys follow the scheme in
   `docs/specs/002-prose-catalogue.md`; the content loader rejects unknown or
   missing keys and placeholders a key is not allowed to use.
2. **Polish** `polish.sh draft.tsv > polished.tsv`: sends the draft to the agy
   CLI with `voice.md` and keeps the draft for any line whose numbers or
   placeholders changed (reported on stderr). agy needs its Google hosts
   reachable; one call handles about 25 lines, so split a large draft:
   `split -l 25 -d draft.tsv part-` and polish each part.
3. **Convert** `to_json.py polished.tsv ../../data/strings.json`: writes the
   catalogue sorted and indented. The `strings_test` parity check fails CI if
   the draft and the catalogue disagree on keys or placeholders.
