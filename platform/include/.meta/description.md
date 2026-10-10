# include

**Purpose:** Public include root of the `platform` static library. `platform/CMakeLists.txt` exports this directory with `target_include_directories(platform PUBLIC include)`, so consumers write `#include "platform/services.h"`.

## Key Components
- `platform/` — namespace-named subdirectory containing `services.h`, the library's single public header.

## Dependencies
- None of its own; it contains no files directly. The header beneath it depends only on the C++ standard library.
- Consumed by targets linking `dc::platform`: `deep_claim` and `deep_claim_layout_probe` (both defined in `game/CMakeLists.txt`).

## Patterns
- `include/<library-name>/<header>` layout so include paths are prefixed with the library name (`platform/...`) and match the `platform` C++ namespace.

## Constraints
- Everything under this directory is public API of `dc::platform`; private code lives in `platform/src/`.
- Holds no files directly, only the `platform/` subdirectory.
