Check the firmware before you copy it to the Pico: its SHA-256 must be the
one in `SHA256SUMS`. On Linux, `sha256sum -c SHA256SUMS` in the folder that
holds both files; on macOS, `shasum -a 256 -c SHA256SUMS`.

Better still, build it yourself from the source code and compare: the build
is reproducible on Linux x86_64, so the same tag gives the same file. See
[the Pico port](https://github.com/ndssigner/dtmf-signer/blob/main/ports/pico/README.md).
