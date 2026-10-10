# include

**Purpose:** Public include root of the `sim` static library. It exists only to provide the `sim/` path prefix, so every consumer includes headers as `"sim/<name>.h"`.

## Key Components
- `sim/` — the 29 public headers of the simulation library (types, content structs, commands, world state, views, per-system rule APIs, serialization, save/load, `Simulation`).

## Dependencies
- None of its own. `sim/CMakeLists.txt` registers this directory with `target_include_directories(sim PUBLIC include)`, so it is propagated to every target that links `dc::sim` (`content`, `game_session`, `sim_runner`, `sim_tests`, and transitively the `deep_claim` game client).

## Patterns
- Conventional CMake `include/<libname>/` layout: the library name is part of the include path, so headers cannot collide with those of `content/`, `platform/` or `game/`.

## Constraints
- No files live directly in this directory; headers belong in `sim/`.
- Everything under this directory is public API of `dc::sim`; there are no private headers in the library (`sim/src/` holds only `.cpp` files).
