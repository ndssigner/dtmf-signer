#!/bin/sh
# Builds the Pico firmware the way the published one is built: the Pico SDK
# 2.3.1 and Arm's GNU toolchain 14.3.rel1, both pinned (the toolchain is
# checked against Arm's SHA-256). The published UF2 is built on Linux x86_64
# (the project's CI); a clean checkout of a tag built there gives the same
# UF2, byte for byte. On other hosts the firmware is the same program, but
# Arm's toolchain for each host lays the code out slightly differently, so
# the SHA-256 differs: to check a release, build on Linux x86_64 (or in a
# Linux x86_64 container, see ports/pico/README.md).
#
#   tools/build-pico.sh            # build; prints the UF2's path and SHA-256
#
# Needs git, cmake (3.22 or later), ninja or make, python3, curl and tar,
# and picotool 2.3.1: installed (Homebrew: brew install picotool), or else
# the SDK builds it, which takes a C++ compiler for the host
# (build-essential and libusb-1.0-0-dev on Debian or Ubuntu). Runs on Linux (x86_64, aarch64) and
# on macOS with Apple silicon; Windows through WSL. What it downloads goes to
# ~/.cache/dtmf-signer (or $DS_CACHE); nothing is installed system-wide.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CACHE=${DS_CACHE:-$HOME/.cache/dtmf-signer}
SDK_TAG=2.3.1
GCC_VER=14.3.rel1

say() { printf '%s\n' "$*"; }
die() { printf 'build-pico: %s\n' "$*" >&2; exit 1; }

case "$(uname -s)-$(uname -m)" in
Linux-x86_64)
    GCC_HOST=x86_64
    GCC_SHA=8f6903f8ceb084d9227b9ef991490413014d991874a1e34074443c2a72b14dbd ;;
Linux-aarch64 | Linux-arm64)
    GCC_HOST=aarch64
    GCC_SHA=2d465847eb1d05f876270494f51034de9ace9abe87a4222d079f3360240184d3 ;;
Darwin-arm64)
    GCC_HOST=darwin-arm64
    GCC_SHA=30f4d08b219190a37cded6aa796f4549504902c53cfc3c7e044a8490b6eba1f7 ;;
*)
    die "no pinned toolchain for $(uname -s) $(uname -m) (Arm publishes $GCC_VER for Linux x86_64 and aarch64 and for Apple silicon). Use the UF2 attached to the release, or build on one of those." ;;
esac

for tool in git cmake python3 curl tar; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool is missing"
done
if command -v sha256sum >/dev/null 2>&1; then
    sha256() { sha256sum "$1" | cut -d' ' -f1; }
else
    sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }
fi
if command -v ninja >/dev/null 2>&1; then
    GEN=Ninja
else
    GEN="Unix Makefiles"
fi
mkdir -p "$CACHE"

# Arm's toolchain, checked before it is unpacked.
GCC_DIR=$CACHE/arm-gnu-toolchain-$GCC_VER-$GCC_HOST
if [ ! -x "$GCC_DIR/bin/arm-none-eabi-gcc" ]; then
    f=arm-gnu-toolchain-$GCC_VER-$GCC_HOST-arm-none-eabi.tar.xz
    say "Downloading Arm GNU Toolchain $GCC_VER ($GCC_HOST)..."
    curl -fL --progress-bar -o "$CACHE/$f" \
        "https://developer.arm.com/-/media/Files/downloads/gnu/$GCC_VER/binrel/$f"
    [ "$(sha256 "$CACHE/$f")" = "$GCC_SHA" ] || { rm -f "$CACHE/$f"; die "$f does not match its SHA-256"; }
    mkdir -p "$GCC_DIR"
    tar -xJf "$CACHE/$f" -C "$GCC_DIR" --strip-components=1
    rm -f "$CACHE/$f"
fi

# The Pico SDK at its tag, with only the submodule the firmware needs.
SDK_DIR=$CACHE/pico-sdk-$SDK_TAG
if [ ! -f "$SDK_DIR/pico_sdk_init.cmake" ]; then
    say "Fetching the Pico SDK $SDK_TAG..."
    rm -rf "$SDK_DIR"
    git clone -q --depth 1 --branch "$SDK_TAG" https://github.com/raspberrypi/pico-sdk "$SDK_DIR"
    git -C "$SDK_DIR" submodule -q update --init --depth 1 lib/tinyusb
fi

# Our own submodules (secp256k1, ur-tones, qrcodegen).
git -C "$ROOT" submodule -q update --init --recursive

# The version the About page shows comes from git describe: build from a
# clone of the repository, not from a downloaded archive.
git -C "$ROOT" describe --tags --always >/dev/null 2>&1 || die "not a git clone: the version would be unknown"

# Always from scratch, in a folder of its own.
BUILD=${DS_BUILD:-$ROOT/build-pico-release}
rm -rf "$BUILD"
PATH=$GCC_DIR/bin:$PATH
export PATH
cmake -S "$ROOT/ports/pico" -B "$BUILD" -G "$GEN" -DCMAKE_BUILD_TYPE=Release \
    -DPICO_SDK_PATH="$SDK_DIR" -DPICO_TOOLCHAIN_PATH="$GCC_DIR" >/dev/null
cmake --build "$BUILD" >/dev/null

UF2=$BUILD/dtmf-signer-pico.uf2
[ -f "$UF2" ] || die "the build did not make $UF2"
say ""
say "Built: $UF2"
say "Version: $(git -C "$ROOT" describe --tags --always --dirty)"
say "SHA-256: $(sha256 "$UF2")"
if [ "$GCC_HOST" != x86_64 ]; then
    say "(built on $GCC_HOST: the published UF2 is built on Linux x86_64, and"
    say " only a build there matches its SHA-256 byte for byte)"
fi
say ""
say "To flash it: hold BOOTSEL while plugging the Pico in, then copy the UF2"
say "onto the drive that appears (RPI-RP2)."
