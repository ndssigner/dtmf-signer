#!/bin/sh
# The DS port's end-to-end test in melonDS (development only).
#
#   tools/build-nds.sh --autotest
#   tools/dev/nds_e2e.sh MELONDS [NDS]
#
# MELONDS is a melonDS .app bundle or a melonDS binary; NDS defaults to
# build-nds/dtmf-signer-ds-autotest.nds. The AUTOTEST build plays the test
# seed and PSBT as tones in place of the microphone and presses the keys
# itself (ports/nds/arm9/autotest.h); this script only starts it and reads
# its verdict from melonDS's output (the no$gba debug messages the build
# prints): "AUTOTEST PASS <sha256 of the signed PSBT>" or "AUTOTEST FAIL
# <step>", also shown on the bottom screen. Exit status 0 on PASS.
#
# melonDS has no command-line options for its settings, so for the run its
# melonDS.toml gets DS mode with the built-in BIOS and firmware, direct
# boot, no frame limit and no microphone; the user's file is put back
# exactly as it was afterwards (checked by its SHA-256). Emulated time is
# unaffected by the frame limit: the timings ("t pin|derive|sign <ms>") are
# a DS's.
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TIMEOUT=${NDS_E2E_TIMEOUT:-600}

die() { printf 'nds_e2e: %s\n' "$*" >&2; exit 2; }

[ $# -ge 1 ] && [ $# -le 2 ] || die "usage: tools/dev/nds_e2e.sh MELONDS [NDS]"
EMU=$1
[ -d "$EMU" ] && EMU=$EMU/Contents/MacOS/melonDS # an .app bundle
[ -x "$EMU" ] || die "not a melonDS binary: $EMU"
NDS=${2:-$ROOT/build-nds/dtmf-signer-ds-autotest.nds}
[ -f "$NDS" ] || die "no $NDS: run tools/build-nds.sh --autotest"
PNAME=$(basename "$EMU")
pgrep -x "$PNAME" >/dev/null 2>&1 && die "melonDS is running: close it first"

# melonDS's config directory: macOS, or Linux (XDG).
if [ -d "$HOME/Library/Preferences" ]; then
    CFG_DIR=$HOME/Library/Preferences/melonDS
else
    CFG_DIR=${XDG_CONFIG_HOME:-$HOME/.config}/melonDS
fi
if command -v sha256sum >/dev/null 2>&1; then
    sha256() { sha256sum < "$1" | cut -d' ' -f1; }
else
    sha256() { shasum -a 256 < "$1" | cut -d' ' -f1; }
fi
CFG=$CFG_DIR/melonDS.toml
TMP=$(mktemp -d)
LOG=$TMP/melonDS.log
SUM=
KEEP=
[ -f "$CFG" ] && { cp -p "$CFG" "$TMP/user.toml"; SUM=$(sha256 "$CFG"); }

# melonDS writes its config when it quits: it must be gone before the
# user's file goes back. (Any melonDS running is this run's: the script
# refuses to start beside another one.)
stop_melonds() {
    [ -n "${PID:-}" ] || return 0
    pkill -P "$PID" 2>/dev/null || true
    pkill -x "$PNAME" 2>/dev/null || true
    kill "$PID" 2>/dev/null || true
    n=0
    while { kill -0 "$PID" 2>/dev/null || pgrep -x "$PNAME" >/dev/null 2>&1; } && [ $n -lt 10 ]; do
        sleep 0.5
        n=$((n + 1))
    done
    pkill -9 -P "$PID" 2>/dev/null || true
    pkill -9 -x "$PNAME" 2>/dev/null || true
    kill -9 "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
    PID=
}

restore() {
    stop_melonds
    if [ -n "$SUM" ]; then
        cp -p "$TMP/user.toml" "$CFG"
        if [ "$(sha256 "$CFG")" != "$SUM" ]; then
            printf 'nds_e2e: %s NOT restored: the original is in %s\n' "$CFG" "$TMP/user.toml" >&2
            KEEP=1
        fi
    else
        rm -f "$CFG"
    fi
    [ -n "$KEEP" ] || rm -rf "$TMP"
}
trap restore EXIT
trap 'exit 3' INT TERM

# The run's settings: key = value in [section] ("" for the top), set or added.
mkdir -p "$CFG_DIR"
{ [ -f "$TMP/user.toml" ] && cat "$TMP/user.toml"; true; } | awk '
    BEGIN {
        want["Emu", "ConsoleType"] = "0"; want["Emu", "ExternalBIOSEnable"] = "false"
        want["Emu", "DirectBoot"] = "true"; want["", "LimitFPS"] = "false"
        want["Mic", "InputType"] = "0"
    }
    function flush(   k, p) {
        for (k in want) { split(k, p, SUBSEP)
            if (p[1] == sec && !(k in done)) { print p[2] " = " want[k]; done[k] = 1 } }
    }
    /^\[/ { flush(); sec = substr($0, 2, length($0) - 2); print; next }
    { split($0, kv, " = "); k = sec SUBSEP kv[1]
      if (k in want) { print kv[1] " = " want[k]; done[k] = 1 } else print }
    END {
        flush()
        for (k in want) if (!(k in done)) {
            split(k, p, SUBSEP); sec = p[1]; print "[" sec "]"; flush()
        }
    }' > "$TMP/run.toml"
cp "$TMP/run.toml" "$CFG"

# script(1) gives melonDS a terminal, so its output is not held in a buffer.
# (BSD script takes the command as arguments, util-linux's with -c.)
if script --version 2>&1 | grep -q util-linux; then
    E_EMU=$EMU E_NDS=$NDS script -q -c 'exec "$E_EMU" "$E_NDS"' "$LOG" >/dev/null 2>&1 &
else
    script -q "$LOG" "$EMU" "$NDS" >/dev/null 2>&1 &
fi
PID=$!

i=0
while [ $i -lt "$TIMEOUT" ]; do
    grep -Eq 'AUTOTEST (PASS|FAIL)' "$LOG" 2>/dev/null && break
    kill -0 "$PID" 2>/dev/null || break
    sleep 1
    i=$((i + 1))
done

tr -d '\r' < "$LOG" | grep -E '^(t |state |fingerprint |AUTOTEST )' || true
RESULT=$(tr -d '\r' < "$LOG" | grep -E '^AUTOTEST (PASS|FAIL)' | head -n 1 || true)
case "$RESULT" in
    "AUTOTEST PASS "*) printf '\n%s (%ss)\n' "$RESULT" "$i"; exit 0 ;;
esac
KEEP=1
printf '\n%s (melonDS log: %s)\n' "${RESULT:-nds_e2e: no verdict after ${i}s}" "$LOG"
exit 1
