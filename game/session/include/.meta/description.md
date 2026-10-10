# include

**Purpose:** The PUBLIC include root of the `game_session` CMake target. It exists only to give the headers the `game/` prefix, so consumers write `#include "game/session.h"`.

## Key Components
- `game/` - The six public headers of the library: `session.h`, `auto_pause.h`, `format.h`, `trace.h`, `replay.h`, `png.h`.

## Dependencies
- Declared by `target_include_directories(game_session PUBLIC include)` in `game/session/CMakeLists.txt`; any target linking `dc::game_session` inherits it.
- The headers themselves include `sim/...` headers, which arrive through the PUBLIC link to `dc::sim`.

## Patterns
- `include/<namespace>/` layout: directory name matches the C++ namespace `game`.

## Constraints
- No files are tracked directly in this directory; headers belong under `game/`.
