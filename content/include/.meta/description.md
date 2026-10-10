# include

**Purpose:** Public include root of the `content` target. CMake exports this directory (`target_include_directories(content PUBLIC include)`), so consumers write `#include "content/loader.h"`.

## Key Components
- `content/` - the namespaced header directory; holds `loader.h`, the only public header of the library.

## Dependencies
- None of its own; the header inside depends on `sim/content.h` from `dc::sim`.

## Patterns
- `include/<target-name>/` layout so that includes are prefixed with the library name, matching `sim/include/sim/` and `platform/include/platform/`.

## Constraints
- No files live directly in this directory; headers go under `content/`.
