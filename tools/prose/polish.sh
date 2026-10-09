#!/usr/bin/env bash
# Polish game prose with the agy CLI.
#
#   tools/prose/polish.sh draft.txt > polished.txt
#
# draft.txt holds one string per line as "key<TAB>text". Placeholders look
# like {amount} or {plot}. The output has the same keys in the same order
# with polished text. Lines whose placeholders, numbers or ids changed are
# reported on stderr and left as the draft, so nothing silently breaks.
set -euo pipefail
input="${1:?usage: polish.sh draft.txt}"
voice="$(dirname "$0")/voice.md"
prompt="You are the writer for Deep Claim, a mining-company management game. Do not use any tools, run any command or read any file: everything you need is in this message, and your reply is plain text. Rewrite each line below so it reads as natural human writing in the voice described, not as machine-generated text. Rules: keep every number, unit, currency figure, plot number (#N), resource name and {placeholder} exactly as given; keep each line to one or two sentences; keep the key unchanged; reply with the lines only, one per input line, in the same order, formatted key<TAB>text, nothing else.

VOICE
$(cat "$voice")

LINES
$(cat "$input")"
out="$(agy -p "$prompt" --output-format text --log-file "${TMPDIR:-/tmp}/agy-polish.log" 2>/dev/null | { grep -P '^[A-Za-z0-9_.-]+\t' || true; })"
# What must survive a rewrite: placeholders, plot numbers, and every figure
# with its sign, currency prefix, magnitude suffix and the unit word that
# follows it, so "180 days" cannot become "180 months" or "$30" become "30".
invariants() {
  printf '%s' "$1" | { grep -oE '\{[a-z_]+\}|#[0-9]+|-?\$?[0-9][0-9,.]*[kMB%]?(/t| t\b| days?| months?| a year)?' || true; } | sort | tr '\n' ' '
}
# Verify invariants line by line.
while IFS=$'\t' read -r key draft; do
  polished="$(printf '%s\n' "$out" | awk -F'\t' -v k="$key" '$1==k {sub(/^[^\t]*\t/, ""); print; exit}')"
  if [ -z "$polished" ]; then
    echo "MISSING $key (kept draft)" >&2; printf '%s\t%s\n' "$key" "$draft"; continue
  fi
  case "$key" in
    *.name) # names are identifiers the player sees everywhere; never rewritten
      [ "$polished" != "$draft" ] && echo "NAME $key: kept draft" >&2
      printf '%s\t%s\n' "$key" "$draft"; continue;;
  esac
  if [ "$(invariants "$draft")" != "$(invariants "$polished")" ]; then
    echo "CHANGED $key: numbers, units or placeholders differ (kept draft)" >&2; printf '%s\t%s\n' "$key" "$draft"; continue
  fi
  printf '%s\t%s\n' "$key" "$polished"
done < "$input"
