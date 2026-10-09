#!/usr/bin/env python3
"""Turn a polished prose TSV into data/strings.json.

    tools/prose/to_json.py tools/prose/polished.tsv data/strings.json

Each input line is "key<TAB>text". Keys are written sorted, two-space
indented, with a trailing newline, so the file diffs cleanly. The script
refuses duplicate keys and lines without a tab.
"""
import json
import sys


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    source, target = argv[1], argv[2]
    strings: dict[str, str] = {}
    with open(source, encoding="utf-8") as handle:
        for number, line in enumerate(handle, start=1):
            line = line.rstrip("\n")
            if not line.strip():
                continue
            if "\t" not in line:
                print(f"{source}:{number}: no tab between key and text", file=sys.stderr)
                return 1
            key, text = line.split("\t", 1)
            if key in strings:
                print(f"{source}:{number}: duplicate key {key}", file=sys.stderr)
                return 1
            strings[key] = text
    with open(target, "w", encoding="utf-8") as handle:
        json.dump(dict(sorted(strings.items())), handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    print(f"{len(strings)} strings written to {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
