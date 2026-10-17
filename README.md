# Deep Claim (working title)

Single-player mining company management sim. See `docs/DESIGN.md` for
architecture decisions and `vcpkg.json` for dependencies.

A game ends. Two companies race to complete a three-stage space program
before a deadline at the end of year 30 (day 10,800); the first to finish
founds it, and if nobody does by the deadline, every company loses.

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

## Playtest builds: `./build.sh`

```sh
./build.sh                  # fetch the Windows playtest package for this commit
./build.sh --linux          # build the client here and run it
./build.sh --linux --no-run # build only
```

By default the script fetches the Windows playtest package that CI built
for the checked-out commit and puts it in `~/snorkel/deep-claim`
(`DEEP_CLAIM_SHARE` sets another folder). The commit must be pushed, and
the `gh` CLI must be signed in. If CI is still building, the script waits.
It writes three launchers beside the game: `Play.bat`,
`Play (Deck size).bat` and `Play (with log).bat`. Traces and screenshots
go to the `playtest` folder there, which is kept between builds.

`--linux` builds the `release-game` preset and runs the client; any other
argument, such as `--deck`, is passed to the game.

## Balance runs

```sh
./build/release/sim_runner/sim_runner --seed 7 --years 20 --ai 2 --sanity --csv run.csv
```

Every company is driven by the built-in AI; the JSON summary lists outcomes,
price statistics and sanity checks. A run stops when its game ends, so
`--years 30` always reaches an outcome. With `--sanity` a single run exits
with code 3 if any of its checks fails. A sweep (`--sweep N`) exits with
code 3 when a gated check fails, with or without `--sanity`; checks printed
with `"gated": false` are reported for information.
