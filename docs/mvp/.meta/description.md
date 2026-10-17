# mvp

**Purpose:** The etc harness's home for product-definition material about the minimum playable game. It holds only `journeys/`, the player journeys that specs from 008 on are written to serve.

## Key Components
- `journeys/` - three player journeys, `J-001` to `J-003` (`J-NNN-<slug>.md`), written with the `/journey` skill and served by specs 008, 009 and 010 (see its own description).

## Dependencies
- Listed in `PROJECT.md` ("Customer journeys: `docs/mvp/journeys/`"); referenced by the specs in `spec/`. No code, test or CI job reads this directory.

## Patterns
- Markdown with YAML front matter, one file per journey, in the subdirectory only; nothing sits at this level.

## Constraints
- None identified beyond those of `journeys/`.
