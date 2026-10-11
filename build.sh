#!/usr/bin/env bash
# Put the newest Windows playtest build on the share drive.
#
#   ./build.sh              fetch the Windows build of this commit from CI
#                           into ~/snorkel/deep-claim; on the laptop, open
#                           that folder and run Play.bat
#   ./build.sh --linux      build the client here and run it instead
#   ./build.sh --linux --no-run     ... build only
#
# The Windows build is made by the "playtest package" job of the CI workflow,
# so the commit has to be pushed first. If CI is still building it, the script
# waits. Set DEEP_CLAIM_SHARE to use another folder. With --linux, any other
# argument (such as --deck) is passed to the game.
set -euo pipefail
cd "$(dirname "$0")"

share="${DEEP_CLAIM_SHARE:-$HOME/snorkel/deep-claim}"
artifact="deep-claim-windows"

linux=0
run=1
game_args=()
for arg in "$@"; do
    case "$arg" in
        --linux) linux=1 ;;
        --no-run) run=0 ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) game_args+=("$arg") ;;
    esac
done

describe() {
    echo "$(git log -1 --format='%h %s') on $(git branch --show-current)"
}

if [ "$linux" = 1 ]; then
    # First run only: the vcpkg submodule and its bootstrap.
    if [ ! -x external/vcpkg/vcpkg ]; then
        git submodule update --init
        ./external/vcpkg/bootstrap-vcpkg.sh -disableMetrics
    fi
    cmake --preset release-game
    cmake --build --preset release-game --parallel
    echo
    echo "Built $(describe)"
    if [ "$run" = 1 ]; then
        exec ./build/release-game/game/deep_claim "${game_args[@]}"
    fi
    exit 0
fi

sha="$(git rev-parse HEAD)"
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "build.sh: note: uncommitted changes are not in the CI build" >&2
fi

# The newest CI run for exactly this commit.
run_id="$(gh run list --workflow CI --commit "$sha" --limit 1 --json databaseId \
    --jq '.[0].databaseId // empty')"
if [ -z "$run_id" ]; then
    echo "build.sh: CI has no run for $(describe)." >&2
    echo "          Push it first (git push), then run this again." >&2
    exit 1
fi

# Wait for the package job if it is still going; other jobs may run on.
while :; do
    state="$(gh run view "$run_id" --json jobs --jq \
        '.jobs[] | select(.name | endswith("playtest package")) | .status + " " + (.conclusion // "")')"
    case "$state" in
        "completed success") break ;;
        completed*)
            echo "build.sh: the playtest package job failed: gh run view $run_id" >&2
            exit 1 ;;
        "")
            echo "build.sh: this commit's CI run has no playtest package job" >&2
            exit 1 ;;
        *)
            echo "CI is still building the package (${state% }); checking again in 30 s"
            sleep 30 ;;
    esac
done

# The artifact is published when the whole run finishes; wait for that too.
fresh="$(mktemp -d)"
trap 'rm -rf "$fresh"' EXIT
until gh run download "$run_id" --name "$artifact" --dir "$fresh" 2>/dev/null; do
    echo "Package built; waiting for CI to publish it"
    sleep 20
done
test -f "$fresh/deep_claim.exe"

# Replace the game's files; leave the playtest folder (traces, screenshots,
# save copies) alone.
mkdir -p "$share/playtest"
find "$share" -mindepth 1 -maxdepth 1 ! -name playtest -exec rm -r -- {} +
cp -r "$fresh"/. "$share"/

# Launchers: traces go to the playtest folder beside the game, on the share.
launcher() {
    printf '@echo off\r\nset "DEEP_CLAIM_PLAYTEST=%%~dp0playtest"\r\nstart "" "%%~dp0deep_claim.exe"%s\r\n' "$2" \
        > "$share/$1"
}
launcher "Play.bat" ""
launcher "Play (Deck size).bat" " --deck"

echo
echo "Ready in $share: $(describe)"
echo "On the laptop, open that folder on the share and run Play.bat."
echo "Traces and screenshots land in its playtest folder."
