# DTMF-Signer en la Raspberry Pi Pico (RP2040)

El firmware del prototipo de protoboard: **escucha por cable** (tonos DTMF en
una entrada de línea) y **habla por QR** en una OLED de 128 × 64, con cinco
botones en una escalera de resistencias. Es un proyecto CMake propio que
reutiliza `core/` y `app/` sin cambios; este directorio solo contiene el
hardware que los rodea (ADC y DMA, pantalla, botones, el eco por USB). El
perfil es
[`PROFILE-PICO-REF`](../../docs/es/catalogo.md#profile-pico-ref--referencia-pico)
y los tiempos medidos en esta placa están en el
[catálogo](../../docs/es/catalogo.md).

## Compilar

Hace falta el SDK de la Raspberry Pi Pico 2.3.1, una cadena de herramientas
Arm GCC, CMake 3.22 o posterior y el repositorio clonado con
`--recurse-submodules`.

```bash
cmake -S ports/pico -B build-pico -DPICO_SDK_PATH=<ruta al SDK de la Pico 2.3.1>
cmake --build build-pico
```

Da `build-pico/dtmf-signer-pico.uf2`. Opciones:

| Opción | Valores | |
| --- | --- | --- |
| `DS_DISPLAY` | `SH1106_SPI` (por defecto), `SSD1306_SPI`, `SSD1306_I2C` | `SH1106_SPI` es la probada en hardware. `SSD1306_SPI` y `SSD1306_I2C` compilan, pero **no están probadas** en hardware. `SSD1306_I2C` usa I2C0 con SDA en GP4, SCL en GP5 y la dirección 0x3C. |
| `DS_DISPLAY_FLIP` | `ON` (por defecto), `OFF` | Gira la imagen 180 grados. `ON` es lo correcto para la [disposición en protoboard](../../hardware/breadboard/README.es.md); `OFF`, para una pantalla montada al revés. Las dos están probadas. |

## Cargar el firmware

Mantén pulsado BOOTSEL mientras enchufas el cable USB y luego:

```bash
picotool load -x build-pico/dtmf-signer-pico.uf2
```

El firmware desactiva a propósito el reinicio al cargador por USB (el
ordenador no debe poder reiniciar el dispositivo en su cargador), así que
`picotool load -f` no funciona: usa siempre BOOTSEL.

## Pines

El único sitio donde se nombran los pines es [`board.h`](board.h).

| Pin | Uso |
| --- | --- |
| GP17 | CS de la OLED (SPI0) |
| GP18 | SCK de la OLED (SPI0) |
| GP19 | MOSI de la OLED (SPI0) |
| GP20 | DC de la OLED |
| GP21 | RES de la OLED |
| GP4, GP5 | SDA, SCL de la OLED (solo con `SSD1306_I2C`, I2C0, dirección 0x3C) |
| GP25 | El LED de la placa |
| GP27 (ADC1) | Entrada de línea |
| GP28 (ADC2) | Escalera de botones |

El ADC recorre ambas entradas por turnos a 32 kHz en total (16 kHz para la
línea, una lectura de la escalera a 1 kHz) y un canal DMA escribe en un
anillo. La escalera da cinco botones: IZQUIERDA (LEFT), ARRIBA (UP), OK,
ABAJO (DOWN) y DERECHA (RIGHT).

## Controles

- **Menús:** ARRIBA y ABAJO mueven, OK elige, IZQUIERDA vuelve.
- **El selector** (el PIN, la passphrase): ARRIBA y ABAJO cambian la letra,
  DERECHA la añade, IZQUIERDA borra la última, OK termina. La letra que se
  está eligiendo solo se añade con DERECHA: terminar con OK no la añade.
- **Confirmar una firma:** en la última página de la revisión, una pulsación
  corta de cualquier botón, al menos 300 ms después de que aparezca la
  revisión, y luego mantener OK 1,5 s. Una pulsación larga empezada antes de
  que la revisión estuviera en pantalla nunca confirma. Si no, no se firma
  nada.
- **Pantallas que pone un trabajo** (las huellas, los errores, el QR)
  ignoran las pulsaciones durante los primeros 300 ms.
- **En la pantalla de la huella de la semilla,** IZQUIERDA descarta la
  semilla y lo borra todo.
- **Borrado de emergencia:** mantener IZQUIERDA 3 s, en cualquier pantalla.
  Todo se pone a cero y la placa se reinicia. Mientras "Working..." está en
  pantalla no se leen los botones, así que los 3 s solo empiezan a contar
  cuando el trabajo ha terminado.

## El eco por USB

El puerto serie USB es **solo de salida**: el firmador nunca lee de él.
Imprime datos públicos y nunca un secreto: los nombres de estado, los
tiempos (`t pin|derive|review|sign|qr <ms>`), las huellas, el descriptor, la
PSBT firmada en trozos de base64 entre `signed-psbt-begin` y
`signed-psbt-end` y, cada 5 s, `stack-free`. Compara la PSBT firmada con la
salida de la versión para ordenador para comprobar el firmware (las líneas
exactas, en [`app/ds_app.h`](../../app/ds_app.h)).

## Memoria

La pila del núcleo 0 es de 8 KB, extendida sobre los dos bancos scratch (la
del SDK es de 4 KB). El núcleo 1 no se arranca nunca, y el enlazador falla si
algo se coloca donde necesitaría su propia pila. La pila libre mínima vista
fue de 2692 de 8192 bytes, en la revisión y la firma; la RAM estática (`.data`
y `.bss`) es de unos 116 KB de 264 KB.

## Qué está probado

Probado en el prototipo de protoboard (SH1106 por SPI): la semilla por tonos
con su PIN, las huellas, la XPUB como QR leído por Sparrow, dos PSBT firmadas
byte a byte igual que la versión para ordenador, un cambio falso rechazado y
el borrado de emergencia.

**Sin probar en hardware:** `DS_DISPLAY=SSD1306_SPI` y `SSD1306_I2C`,
otras etapas de entrada de línea y la RP2350.
