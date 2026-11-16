# DTMF-Signer como WebAssembly

La placa del simulador de la web: los mismos `core/` y `app/` que la Pico,
compilados a WebAssembly, con JavaScript en lugar del hardware. Recibe las
muestras de la entrada de línea, el botón pulsado (una vez por milisegundo, a
través del mismo filtro de rebotes que la escalera de resistencias de la
Pico) y el reloj, y devuelve la pantalla, su contraste y el eco público. Nada
de `core/` ni de `app/` cambia por él: lo que pruebas en el navegador es lo
que ejecuta la placa.

## Compilarlo

```sh
tools/build-wasm.sh
```

Ejecuta Emscripten desde una imagen Docker fijada por digest, siempre como
`linux/amd64`, y escribe `build/wasm/dtmf-signer.mjs` (un módulo ES con el
wasm dentro) con su SHA-256: el mismo commit da el mismo archivo en cualquier
máquina, y el CI de la web imprime el hash del que publica.

## Sus funciones

`dsw_init`, `dsw_audio_buf` y `dsw_audio` (muestras a 16 kHz), `dsw_advance`
(milisegundos con una clase de botón pulsada), `dsw_take_dirty`, `dsw_draw`
(el framebuffer de 128 × 64), `dsw_contrast`, `dsw_rows` y `dsw_line` (el
texto de la pantalla), `dsw_take_echo` y `dsw_echo`, `dsw_keys`,
`dsw_wants_reboot`, `dsw_version`, `dsw_listening` y `dsw_qr_shown` (lo
que anima el dibujo del simulador), y `dsw_picker_cell` para los tests. Mira
`main.c`.
