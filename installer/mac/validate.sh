#!/usr/bin/env bash
# Nam KNoB — load the built VST3 and AU the way DAWs do and run them through the standard validators.
#   installer/mac/validate.sh <artefacts dir (…/VFXAamps_artefacts/Release)> <VFXAampsHostTest binary>
# 1. our host test (tests/host_tests.cpp) on the VST3 and the AU (re-run under lldb on a crash, for a backtrace)
# 2. pluginval (Tracktion), strictness 10, on the VST3 and the AU
# 3. auval -strict (Apple's AU validator; Logic / GarageBand / MainStage refuse AUs that fail it)
set -uo pipefail
ART="$1"
HOST="$2"
VST3="$ART/VST3/Nam KNoB.vst3"
AU_SRC="$ART/AU/Nam KNoB.component"
AU="$HOME/Library/Audio/Plug-Ins/Components/Nam KNoB.component"
failed=0
arch="$(uname -m)"
echo "Validating on $arch"

run_host() {
    echo "::group::host test: $1"
    if "$HOST" "$1"; then
        echo "host test passed: $1"
    else
        echo "::error::host test failed or crashed on $arch: $1"
        failed=1
        echo "--- again under lldb, for a backtrace ---"
        lldb --batch -o run -k "thread backtrace all" -k quit -- "$HOST" "$1" || true
    fi
    echo "::endgroup::"
}

# AU has to be installed for hosts (and auval) to find it
mkdir -p "$(dirname "$AU")"
rm -rf "$AU"
cp -R "$AU_SRC" "$AU"
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
sleep 2

run_host "$VST3"
run_host "$AU"

echo "::group::auval"
if auval -strict -v aufx VFXh Vfxc; then echo "auval passed"; else echo "::error::auval failed on $arch"; failed=1; fi
echo "::endgroup::"

PV_DIR="$RUNNER_TEMP/pluginval"
mkdir -p "$PV_DIR"
if curl -fsSL -o "$PV_DIR/pluginval.zip" https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip \
   && unzip -q -o "$PV_DIR/pluginval.zip" -d "$PV_DIR"; then
    PV="$PV_DIR/pluginval.app/Contents/MacOS/pluginval"
    for p in "$VST3" "$AU"; do
        echo "::group::pluginval: $p"
        if "$PV" --strictness-level 10 --timeout-ms 900000 --validate "$p"; then
            echo "pluginval passed: $p"
        else
            echo "::error::pluginval failed on $arch: $p"
            failed=1
        fi
        echo "::endgroup::"
    done
else
    echo "::warning::could not download pluginval"
fi

exit $failed
