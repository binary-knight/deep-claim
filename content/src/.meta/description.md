# src

**Purpose:** Implementation of the `content` static library: parses the seven JSON data documents with nlohmann-json, validates every field and cross-reference, and builds a `sim::Content`. All parsing helpers live in an anonymous namespace; only the three functions declared in `content/loader.h` are exported.

## Key Components
- `loader.cpp` (1447 lines) - the whole loader, in this order:
  - `ErrorSink` - appends `LoadError{file, path, message}` to the shared error vector, tagged with one file name.
  - `Node` - a JSON object plus its dotted path (`[2].base_price`); typed accessors `string`, `boolean`, `integer(min,max)`, `fixed(min,max)`, `money(min)`, `object`, `objectArray`, `stringArray`, plus `allowKeys` (reports any key outside a whitelist as `unknown key`) and private `decimal` (finite / magnitude / decimal-places checks). Accessors report to the sink and return `std::nullopt` so parsing continues.
  - Document helpers: `parseDocument` (wraps `json::parse`, catches `json::exception`), `topLevelArray`, `isValidId`, `readId` (a-z, 0-9, underscore; unique per file), `valueOr`.
  - Per-file parsers: `parseResources`, `parseTechs` + `parseTechEffect` + `checkTechCycles`/`hasCycleFrom` (DFS), `resolveUnlocks` (resources `unlocked_by` versus tech `unlock_resource` effects, checked both ways), `parseRegions` + `parseRange` (Money and int64 overloads) + `parseDeposits`, `parseEvents` + `parseEventEffect`, `parseBalance` (fills `sim::BalanceParams`), `parseAi` (fills `sim::AiParams`).
  - Name tables `kTechEffectNames` (6 entries) and `kEventEffectNames` (3 entries) mapping JSON `type` strings to `sim::TechEffectType` / `sim::EventEffectType`, looked up with `findByName`.
  - Prose catalogue: `kStringKeys` (the fixed table of every `notice.*`, `ui.*`, `status.*` key with its allowed placeholders), `templatePlaceholders`, `allowsPlaceholder`, `expectedStringKeys`, `readStringEntry`, `parseStrings`.
  - `readFile` (binary `std::ifstream` into a string).
  - Public definitions: `loadFromStrings`, `loadFromDirectory`, `formatErrors`.

## Dependencies
- `content/loader.h` (own public header).
- `sim/` headers: `sim/commands.h` (`sim::rejectReasonName`, `sim::RejectReason`, `sim::noticeKindName`, `sim::NoticeKind`), `sim/content.h` (`sim::Content`, `ResourceDef`, `TechDef`, `RegionDef`, `EventDef`, `DepositPrior`, `BalanceParams`, `AiParams`, effect types), `sim/fixed.h` (`sim::Fixed`), `sim/ids.h` (`ResourceId`, `TechId`), `sim/money.h` (`sim::Money`), `sim/types.h` (`sim::Day`, `kDaysPerYear`, `kMaxStockTonnes`).
- `<nlohmann/json.hpp>` (private; vcpkg package `nlohmann_json`).
- C++ standard library: `<filesystem>`, `<fstream>`, `<sstream>`, `<cmath>`, `<optional>`, `<array>`, `<algorithm>`.

## Patterns
- Collect-all validation: no parser stops at the first problem; failed fields take a fallback via `valueOr` and parsing continues, so one load reports every error.
- No exceptions escape for bad data: `json::exception` from `json::parse` is caught and turned into a `LoadError` with message `invalid JSON: ...`; `Node` accessors check JSON types before calling `get<T>()`.
- Two-pass reference resolution: tech `requires` names and resource `unlocked_by` names are stored as strings first and resolved to `sim::TechId` after all techs exist.
- Fixed load order in `loadFromStrings`: resources, techs (+ cycle check), `resolveUnlocks`, regions, events, balance, ai, strings. Strings are last because the `event.<id>.*` and `tech.<id>.*` keys depend on parsed ids.
- Strict schemas: every object is checked with `allowKeys`; unknown keys are errors.
- Table-driven string catalogue: expected keys = `kStringKeys` + `event.<id>.name|description` + `tech.<id>.name|description` + `reject.<name>` for each `sim::RejectReason`; enum names are enumerated by calling the sim name function for 0..255 until it returns `"unknown"`.
- Decimals are read as `double`, checked, then converted with `sim::Fixed::fromDouble` or `std::llround` to cents.

## Constraints
- `sim::Content` is returned only when the error list is empty.
- Numeric bounds are enforced at load: `kMaxDays = 36000`, Money magnitude at most `1e11` with at most 2 decimal places, Fixed magnitude at most `1e6` with at most 6 decimal places, effect multipliers within 0.01 to 100.
- `resources.json` and `regions.json` must each contain at least one entry; `techs.json` and `events.json` may be empty arrays. `balance.json`, `ai.json` and `strings.json` must be top-level objects; the other four must be top-level arrays of objects.
- Ids must be non-empty, use only a-z, 0-9 and underscore, and be unique within their file.
- A tech may not require itself and prerequisite cycles are rejected.
- Every `sim::NoticeKind` name must have a `notice.<name>` entry in `kStringKeys`, otherwise a strings.json error is raised even if the data file is complete; adding a notice kind or reject reason in `sim/` therefore requires a matching table/catalogue entry.
- A string template may only use `{placeholders}` listed for its key; placeholder names are a-z and underscore only; a stray `{` or `}` is `malformed placeholder braces`.
- Event and tech `name`/`description` are not read from `events.json`/`techs.json`; they are filled from the strings catalogue.
- Empty strings are rejected for every string field read through `Node::string` and for every catalogue entry.
