#!/bin/sh
# Builds the firmware as WebAssembly for the website's simulator:
# build/wasm/dtmf-signer.mjs and build/wasm/dtmf-signer-voice.mjs (the phone
# build), with Emscripten from a Docker image pinned by
# digest, always as linux/amd64 (the platform of CI), so the same commit
# gives the same files anywhere. Prints their SHA-256.
#     tools/build-wasm.sh
set -eu
EMSDK_IMAGE="emscripten/emsdk:6.0.13@sha256:fb7864e398acfda863dfcea78b8bc8adad359fa60c6a0bbeb845931de64aa68a"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
rm -rf build-wasm
docker run --rm --platform linux/amd64 -v "$ROOT":/src -w /src "$EMSDK_IMAGE" sh -c '
  git config --global --add safe.directory "*" &&
  emcmake cmake -S . -B build-wasm -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release >/dev/null &&
  cmake --build build-wasm --target dtmf-signer-wasm --target dtmf-signer-voice-wasm -j 4 >/dev/null'
mkdir -p build/wasm
cp build-wasm/ports/wasm/dtmf-signer.mjs build/wasm/dtmf-signer.mjs
cp build-wasm/ports/wasm/dtmf-signer-voice.mjs build/wasm/dtmf-signer-voice.mjs
if command -v sha256sum >/dev/null; then SUM=sha256sum; else SUM='shasum -a 256'; fi
$SUM build/wasm/dtmf-signer.mjs build/wasm/dtmf-signer-voice.mjs
