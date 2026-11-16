Comprueba el firmware antes de copiarlo a la Pico: su SHA-256 debe ser el de
`SHA256SUMS`. En Linux, `sha256sum -c SHA256SUMS` en la carpeta que tiene los
dos archivos; en macOS, `shasum -a 256 -c SHA256SUMS`.

Mejor aún, compílalo tú desde el código fuente y compara: la compilación es
reproducible en Linux x86_64, así que la misma etiqueta da el mismo archivo.
Mira [el port de la Pico](https://github.com/ndssigner/dtmf-signer/blob/main/ports/pico/README.es.md).
