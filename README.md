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

## Playing

The client needs SDL3 and RmlUi, which vcpkg builds behind the `game`
manifest feature. On Linux install the windowing headers first, for example
on Ubuntu:

```sh
sudo apt install libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxfixes-dev libxss-dev libxkbcommon-dev libwayland-dev libdecor-0-dev \
  libegl1-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev pkg-config
```

Then:

```sh
cmake --preset release-game        # first run builds SDL3, FreeType and RmlUi
cmake --build --preset release-game
./build/release-game/game/deep_claim
```

Data and assets are copied next to the executable, so the build directory can
be zipped and moved. Saves live in the per-user preference directory SDL
reports (`~/.local/share/DeepClaim/DeepClaim/saves` on Linux,
`%APPDATA%\DeepClaim\DeepClaim\saves` on Windows).

Controls: space pauses, `1` `2` `3` set the speed, `.` steps one day, `F8`
opens the interface debugger.

## Balance runs

```sh
./build/release/sim_runner/sim_runner --seed 7 --years 20 --ai 2 --sanity --csv run.csv
```

Every company is driven by the built-in AI; the JSON summary lists outcomes,
price statistics and sanity checks, and `--sanity` exits non-zero if any
check fails.
