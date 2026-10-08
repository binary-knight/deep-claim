# Deep Claim (working title)

Single-player mining company management sim. See `docs/DESIGN.md` for
architecture decisions and `vcpkg.json` for dependencies.

## Building

```sh
git submodule update --init
./external/vcpkg/bootstrap-vcpkg.sh -disableMetrics   # bootstrap-vcpkg.bat on Windows
cmake --preset debug          # or release, asan
cmake --build --preset debug
ctest --preset debug
```

The game client (SDL3 + RmlUi) is behind the `DC_BUILD_GAME` option and the
`game` vcpkg feature. It is not built yet.
