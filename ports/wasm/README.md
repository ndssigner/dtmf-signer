# DTMF-Signer as WebAssembly

The board of the website's simulator: the same `core/` and `app/` as the
Pico, compiled to WebAssembly, with JavaScript in place of the hardware. It
takes the line-in samples, the button held (once a millisecond, through the
same debouncer as the Pico's resistor ladder) and the clock, and gives back
the screen, its contrast and the public echo. Nothing in `core/` or `app/`
changes for it: what you try in the browser is what the board runs.

## Build it

```sh
tools/build-wasm.sh
```

It runs Emscripten from a Docker image pinned by digest, always as
`linux/amd64`, and writes `build/wasm/dtmf-signer.mjs` (an ES module with the
wasm inside) with its SHA-256: the same commit gives the same file on any
machine, and the website's CI prints the hash of the one it publishes.

## Its functions

`dsw_init`, `dsw_audio_buf` and `dsw_audio` (16 kHz samples), `dsw_advance`
(milliseconds with a button class held), `dsw_take_dirty`, `dsw_draw` (the
128 × 64 framebuffer), `dsw_contrast`, `dsw_rows` and `dsw_line` (the text on
the screen), `dsw_take_echo` and `dsw_echo`, `dsw_keys`, `dsw_wants_reboot`,
`dsw_version`, `dsw_listening` and `dsw_qr_shown` (what the simulator's
drawing animates), and `dsw_picker_cell` for the tests. See `main.c`.
