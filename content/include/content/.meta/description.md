# content (public headers)

**Purpose:** The public API of the `dc::content` library, included as `"content/loader.h"`. It declares the data file names, the error and result types, and the three entry points for loading game data into a `sim::Content`.

## Key Components
- `loader.h` - everything in `namespace content`:
  - File name constants (`inline constexpr const char*`): `kResourcesFile` = `resources.json`, `kTechsFile` = `techs.json`, `kRegionsFile` = `regions.json`, `kEventsFile` = `events.json`, `kBalanceFile` = `balance.json`, `kAiFile` = `ai.json`, `kStringsFile` = `strings.json`.
  - `struct LoadError { std::string file; std::string path; std::string message; }` - one problem, e.g. file `resources.json`, path `[2].base_price`, message `expected a number`.
  - `struct Sources` - the JSON text of each document: `resources`, `techs`, `regions`, `events`, `balance`, `ai`, `strings`.
  - `struct LoadResult { std::optional<sim::Content> content; std::vector<LoadError> errors; bool ok() const; }` - `ok()` is `content.has_value()`.
  - `LoadResult loadFromStrings(const Sources&)` - parse and validate in-memory JSON text.
  - `LoadResult loadFromDirectory(const std::filesystem::path&)` - read the seven files from a directory, then call `loadFromStrings`.
  - `std::string formatErrors(const std::vector<LoadError>&)` - one line per error: `file: path: message` (path segment omitted when empty).

## Dependencies
- `sim/content.h` (for `sim::Content`), from the `dc::sim` target.
- Standard library only otherwise: `<filesystem>`, `<optional>`, `<string>`, `<vector>`. No JSON type appears in the header.

## Patterns
- Result-object error reporting: functions are `[[nodiscard]]` and return `LoadResult`; no exception types are declared.
- String-based core (`loadFromStrings`) with a thin filesystem wrapper (`loadFromDirectory`), so tests can feed JSON text directly.
- `#pragma once`; file names exposed as constants so callers and tests refer to them by symbol.

## Constraints
- `LoadResult::content` is present only when `errors` is empty.
- All seven documents are required; `Sources` has no optional members.
- The header documents that decimals are parsed as doubles, rejected when non-finite or more precise than `sim::Fixed` / `sim::Money` can hold, then rounded to nearest.
