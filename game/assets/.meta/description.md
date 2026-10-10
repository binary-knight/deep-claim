# assets

**Purpose:** Runtime assets of the game client: fonts and the RmlUi interface files. The directory is located at run time either next to the executable (`assets/`) or, in development builds, at this path via the `DC_ASSETS_DIR` compile definition.

## Key Components
- `fonts/` - Seven `.ttf` faces (Barlow, Barlow Condensed, JetBrains Mono) and their three SIL OFL licence files.
- `ui/` - `main.rml`, `main.rcss`, the generated `tokens.rcss` and the generated icon sheet `icons.tga`.

## Dependencies
- Consumed by `game/src/main.cpp` (`locate("assets", DC_ASSETS_DIR)`), `game/src/app.cpp` (`App::loadDocument`) and `game/src/layout_probe.cpp`.
- `game/CMakeLists.txt` target `deep_claim_assets` copies this whole directory to `$<TARGET_FILE_DIR:deep_claim>/assets` on every build.
- Two files under `ui/` are produced by scripts in `tools/design/`: `tokens.rcss` by `tokens_to_rcss.py` from `docs/design/tokens.css`, and `icons.tga` by `icons.py`, which draws the icons procedurally and takes no input file.

## Patterns
- Plain files loaded by path; no packing or asset pipeline in the build.
- Game data (`data/`) is separate from presentation assets and lives outside this directory.

## Constraints
- Only formats RmlUi loads without extra libraries are used: TrueType fonts and uncompressed TGA (the vendored backend has its SDL_image texture loader removed).
- The layout under this directory (`fonts/<name>.ttf`, `ui/main.rml`) is assumed by the client code.
