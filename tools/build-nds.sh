#!/bin/sh
# Builds the Nintendo DS firmware the way the published one is built: in
# Docker, with devkitARM pinned by its image digest (ports/nds/Dockerfile),
# always from scratch. The same commit gives the same .nds, byte for byte,
# on any host that runs Docker (Linux, macOS, Windows).
#
#   tools/build-nds.sh             # prints the .nds's path and SHA-256
#   tools/build-nds.sh --autotest  # development only: the emulator test build
#
# Gives build-nds/dtmf-signer-ds.nds. Needs git and Docker (running).
# --autotest gives build-nds/dtmf-signer-ds-autotest.nds instead (make
# AUTOTEST=1; never released): it first builds the host port in the same
# image and has it sign the test vector, the bytes the DS must give
# (tools/dev/nds_autotest_vector.py). Run it with tools/dev/nds_e2e.sh.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=dtmf-signer-nds-builder

say() { printf '%s\n' "$*"; }
die() { printf 'build-nds: %s\n' "$*" >&2; exit 1; }

AUTOTEST=
case "${1:-}" in
    "") ;;
    --autotest) AUTOTEST=1 ;;
    *) die "usage: tools/build-nds.sh [--autotest]" ;;
esac

for tool in git docker; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool is missing"
done
docker info >/dev/null 2>&1 || die "Docker is not running"
if command -v sha256sum >/dev/null 2>&1; then
    sha256() { sha256sum "$1" | cut -d' ' -f1; }
else
    sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }
fi

# Our own submodules (secp256k1, ur-tones, qrcodegen).
git -C "$ROOT" submodule -q update --init --recursive

# The version the About page shows comes from git describe: build from a
# clone of the repository, not from a downloaded archive.
git -C "$ROOT" describe --tags --always >/dev/null 2>&1 || die "not a git clone: the version would be unknown"

docker build -q -t "$IMAGE" "$ROOT/ports/nds" >/dev/null

# Always from scratch. Run as the caller, so the files it writes are theirs.
rm -rf "$ROOT/ports/nds/build" "$ROOT/ports/nds/dtmf-signer-ds.nds"
if [ -z "$AUTOTEST" ]; then
    docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$ROOT":/source "$IMAGE" \
        make -C ports/nds >/dev/null
    NAME=dtmf-signer-ds.nds
else
    docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$ROOT":/source "$IMAGE" sh -ec '
        cmake --log-level=WARNING -S . -B build-nds/host -DCMAKE_BUILD_TYPE=Release >/dev/null
        cmake --build build-nds/host --target dtmf-signer-host -j4 >/dev/null
        python3 tools/dev/nds_autotest_vector.py build-nds/host/ports/host/dtmf-signer-host \
            ports/nds/build/gen/autotest_vector.h
        make -C ports/nds AUTOTEST=1 >/dev/null'
    NAME=dtmf-signer-ds-autotest.nds
fi

OUT=$ROOT/build-nds
mkdir -p "$OUT"
NDS=$OUT/$NAME
cp "$ROOT/ports/nds/dtmf-signer-ds.nds" "$NDS"
say ""
say "Built: $NDS"
say "Version: $(git -C "$ROOT" describe --tags --always --dirty)"
say "Size: $(wc -c < "$NDS" | tr -d ' ') bytes"
say "SHA-256: $(sha256 "$NDS")"
