# src

**Purpose:** Private implementation of the `dc::platform` library. Contains the single translation unit compiled into the `platform` static library, which implements the local-only `platform::Services` backend.

## Key Components
- `null_services.cpp` — defines `class NullServices final : public Services` inside an anonymous namespace in `namespace platform`, plus the public factory `platform::makeNullServices(baseDirectory, playerName)` which returns `std::make_unique<NullServices>(...)`. Behavior per method:
  - `saveDirectory()` — returns `<baseDirectory>/saves`, calling `std::filesystem::create_directories` on it on every call with the `std::error_code` overload and discarding the error.
  - `unlockAchievement(id)` — inserts the id into a `std::set<std::string>` member.
  - `setStat(id, value)` — stores the value in a `std::map<std::string, std::int64_t>` member, overwriting any previous value for that id.
  - `playerName()` — returns a view of the `std::string` passed to the constructor.

## Dependencies
- `platform/services.h` (from `platform/include/platform/`).
- C++ standard library: `<cstdint>`, `<filesystem>`, `<map>`, `<memory>`, `<set>`, `<string>`, `<string_view>`, `<system_error>`, `<utility>`.
- No third-party libraries and no other project directories.
- Listed as the sole source in `platform/CMakeLists.txt` (`add_library(platform STATIC src/null_services.cpp)`).

## Patterns
- Null Object implementation of the `Services` interface, hidden behind a factory; the class has internal linkage (anonymous namespace) and is `final`.
- Constructor takes its arguments by value and moves them into members.
- Non-throwing filesystem use: the `std::error_code` overload of `create_directories` is used instead of the throwing one.

## Constraints
- `saveDirectory()` never reports failure: if the directory cannot be created the path is still returned and the error is dropped.
- The `saves` subdirectory name is hard-coded.
- Achievement and stat data live only in memory, are never written to disk, and are never read anywhere (no accessor exists).
- No synchronization: the containers are plain `std::set`/`std::map` with no locking.
- No OS-specific branches; the file has no preprocessor conditionals.
- 48 lines total; no function exceeds a few lines.
