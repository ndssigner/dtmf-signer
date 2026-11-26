# DTMF-Signer en la Raspberry Pi Pico (RP2040)

El firmware del prototipo de protoboard: **escucha por cable** (tonos DTMF en
una entrada de línea) y **habla por QR** en una OLED de 128 × 64, con cinco
botones en una escalera de resistencias. Es un proyecto CMake propio que
reutiliza `core/` y `app/` sin cambios; este directorio solo contiene el
hardware que los rodea (ADC y DMA, pantalla, botones, el eco por USB). El
perfil es
[`PROFILE-PICO-REF`](../../docs/es/catalog.md#profile-pico-ref--referencia-pico)
y los tiempos medidos en esta placa están en el
[catálogo](../../docs/es/catalog.md). Con `DS_FIRMWARE=PEX` compila en
su lugar [la Centralita](#la-centralita-pex), el firmware de voz.

## Conseguir el firmware

Dos caminos, que dan el mismo programa.

### El UF2 publicado

Las versiones posteriores a la v0.1.0-alpha llevan `dtmf-signer-pico.uf2`
(Cable y QR), y desde la v0.3.0-alpha también `dtmf-signer-pex.uf2` (la
Centralita), compilados por la CI del proyecto desde la etiqueta de la
versión, con sus SHA-256 en `SHA256SUMS`. Úsalo si no quieres compilarlo: en ese caso confías en esa
compilación en vez de comprobarla tú, una decisión que conviene tomar a
sabiendas. Cualquiera puede comprobarlo para todos compilando la misma
etiqueta en Linux x86_64, como se explica abajo, y comparando el SHA-256.

### Compilarlo uno mismo

Desde un clon del repositorio (no desde un archivo descargado: la versión
que muestra la página About sale de git):

```bash
git clone --recurse-submodules https://github.com/ndssigner/dtmf-signer
cd dtmf-signer
git checkout v0.4.0-alpha    # the release to build; skip it for the latest code
tools/build-pico.sh
```

El script descarga el SDK de la Pico 2.3.1 y la cadena de herramientas GNU
de Arm 14.3.rel1 (comprobada con el SHA-256 de Arm) en
`~/.cache/dtmf-signer`, compila desde cero e imprime la ruta y el SHA-256
del UF2. Compilado en Linux x86_64, como el publicado, coincide con él byte
a byte para la etiqueta de una versión. En otros sistemas el firmware es el
mismo programa, pero la cadena de Arm de cada sistema coloca el código de
forma un poco distinta, así que el SHA-256 cambia; el script lo avisa.
Necesita git, CMake 3.22 o posterior, Ninja o make, Python 3, curl y tar, y
picotool 2.3.1 (`brew install picotool` en macOS; en otros sistemas lo
compila el SDK, lo que pide un compilador de C++ y libusb, `build-essential`
y `libusb-1.0-0-dev` en Debian o Ubuntu). Funciona en Linux (x86_64,
aarch64), en macOS con Apple silicon y en Windows con WSL. Arm no publica la
cadena 14.3 para Mac con Intel: ahí, usa el UF2 publicado o el contenedor de
abajo.

Para comprobar una versión desde un Mac o cualquier otro sistema, compílala
en un contenedor Linux x86_64 con los mismos paquetes que la CI (en un Mac
con Apple silicon dio el UF2 de la CI byte a byte, en unos diez minutos con
emulación):

```bash
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src ubuntu:24.04 sh -c \
  'apt-get update && apt-get install -y --no-install-recommends git ca-certificates cmake ninja-build python3 curl xz-utils build-essential libusb-1.0-0-dev pkg-config && git config --global --add safe.directory "*" && tools/build-pico.sh'
```

Con tu propio SDK y tu cadena de herramientas la compilación es la de
siempre, aunque su UF2 no coincidirá byte a byte con el publicado:

```bash
cmake -S ports/pico -B build-pico -DPICO_SDK_PATH=<ruta al SDK de la Pico 2.3.1>
cmake --build build-pico
```

Opciones:

| Opción | Valores | |
| --- | --- | --- |
| `DS_DISPLAY` | `SH1106_SPI` (por defecto), `SSD1306_SPI`, `SSD1306_I2C` | `SH1106_SPI` es la probada en hardware. `SSD1306_SPI` y `SSD1306_I2C` compilan, pero **no están probadas** en hardware. `SSD1306_I2C` usa I2C0 con SDA en GP4, SCL en GP5 y la dirección 0x3C. |
| `DS_DISPLAY_FLIP` | `ON` (por defecto), `OFF` | Gira la imagen 180 grados. `ON` es lo correcto para la [disposición en protoboard](../../hardware/breadboard/README.es.md); `OFF`, para una pantalla montada al revés. Las dos están probadas. |
| `DS_FIRMWARE` | `CABLE_QR` (por defecto), `PEX` | `CABLE_QR` compila `dtmf-signer-pico.uf2`, el firmware de esta página. `PEX` compila `dtmf-signer-pex.uf2`, el firmware de voz: ver [La Centralita](#la-centralita-pex). |
| `DS_LINE_DEBUG` | `OFF` (por defecto), `ON` | Solo la Centralita (PEX), una ayuda de banco: cada segundo, el eco por USB da el nivel de la línea y lo que ha oído el receptor. Nunca en una versión publicada. |

## Alimentación

Muchas placas RP2040 baratas con conector USB-C no llevan las dos
resistencias de 5,1 kΩ que le piden corriente a un cargador USB-C, así que
un cargador USB-C a USB-C (el de un portátil, por ejemplo) no las enciende.
Un cargador USB-A con un cable USB-A a USB-C funciona. Para usarlo,
siempre un cargador o una batería externa, nunca un ordenador (solo para
grabarlo): así [el eco por
USB](#el-eco-por-usb) no llega a ningún ordenador.

## Cargar el firmware

Mantén pulsado BOOTSEL mientras enchufas el cable USB: la Pico aparece como
una unidad llamada `RPI-RP2`. Copia el UF2 en ella; la Pico se reinicia con
el firmware nuevo. O, con picotool:

```bash
picotool load -x build-pico-release/dtmf-signer-pico.uf2
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
| GP25 | El LED de la placa: un destello breve por cada tono oído, si no apagado |
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
tiempos (`t pin|derive|review|sign|qr <ms>`), las huellas, el descriptor, el
nivel de los tonos de cada grupo oído (`level -32.6 dBFS`), la
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

## La Centralita (PEX)

El firmware de voz (perfil
[`PROFILE-PHONE`](../../docs/es/catalog.md#profile-phone--centralita-phone-exchange-pex)):
un teléfono de tonos es toda la interfaz. No tiene pantalla ni botones. El
firmador alimenta la línea telefónica, habla por ella y oye en ella las
teclas y los tonos de la cartera. La lógica es `app/ds_voice`, la misma que
corre en el simulador de la web; [`voice_main.c`](voice_main.c) es el
hardware que la rodea.

Se compila como el de Cable y QR (arriba), desde un clon del repositorio:

```bash
tools/build-pico.sh pex
```

Da `build-pico-pex-release/dtmf-signer-pex.uf2` y su SHA-256, y se carga
igual (BOOTSEL y copiar el UF2); con paquetes de idioma, como en
[Paquetes de voz](#paquetes-de-voz). Desde la
v0.3.0-alpha, las versiones también lo llevan, compilado por la CI. Con CMake a mano, es
la opción `-DDS_FIRMWARE=PEX`.

Enlaza libspeex sin su codificador (`speex_nb_dec`) y las locuciones en
inglés incorporadas. Tiene que caber en el primer MB de flash, o el enlazado
falla: el segundo MB es la región de los paquetes de voz.

### Paquetes de voz

La Centralita habla inglés; cada otro idioma es un paquete, que se graba en
la flash junto al firmware (ver [`tools/voice`](../../tools/voice/README.md)).
[`tools/install_pex.py`](../../tools/install_pex.py) pone en la Pico el
firmware y los paquetes que elijas:

1. Pon en una carpeta `dtmf-signer-pex.uf2`, `install_pex.py` y los
   paquetes que quieras (`es.bin`, ...), todos de las descargas de la misma
   versión (en la web, o la versión publicada y `app/voice/packs/`). Si
   añades `SHA256SUMS`, el firmware se comprueba con él.
2. En esa carpeta, ejecuta `python3 install_pex.py` (en Windows, `py install_pex.py`).
3. Cuando lo pida, mantén pulsado el botón BOOTSEL de la Pico mientras la
   enchufas. Lo pide dos veces: una para el firmware y otra para los idiomas.

Necesita Python 3.8 o posterior y nada más. Primero comprueba cada archivo
y dice qué idiomas entran; después copia el firmware, espera a que la Pico
vuelva en BOOTSEL y copia los idiomas. Al descolgar, la Centralita los
ofrece. Para cambiarlos más adelante, vuelve a ejecutarlo con otros
paquetes: los anteriores se borran. Caben hasta tres paquetes además del
inglés.

Hacen falta dos copias porque la ROM de arranque de la Pico no graba un
solo UF2 que lleve a la vez el firmware y la región de voces (probado en
una Pico: el firmware entró y los paquetes no), y sí graba cada uno por
separado. `python3 install_pex.py --out FOLDER` escribe los dos archivos,
`1-firmware.uf2` y `2-languages.uf2`, para copiarlos a mano en ese orden,
cada uno con la Pico en BOOTSEL.

### La línea

**Nunca la conectes a una roseta de la red telefónica real:** esa línea
lleva hasta 48 V, y unos 90 V de alterna cuando suena.

El teléfono es un teléfono de tonos completo, con su auricular, enchufado
con su propio cable telefónico a un conector RJ11 hembra de la placa: la
línea son los dos contactos centrales del conector.

El firmador alimenta la línea por sí mismo: los 5 V del USB, subidos a 12 V
con un módulo elevador, y luego una fuente de corriente constante de unos
24 mA (un LM317 con 51 ohmios entre OUT y ADJ). Es la corriente que espera
un teléfono, y un cortocircuito en la línea no hace daño. Con la línea
abierta hay unos 11 V; un teléfono descolgado la deja en 6 a 8 V.

| Pin | Uso |
| --- | --- |
| GP27 (ADC1) | La línea, oída: 100 nF desde la línea a un nodo polarizado a 1,65 V (10 k a 3V3, 10 k a masa), luego 1 k + 4,7 k con 10 nF a masa, como la entrada de línea de arriba |
| GP28 (ADC2) | La tensión de la línea, para el gancho: 47 k desde la línea, 10 k y 100 nF a masa |
| GP15 | La voz hacia la línea: PWM (10 bits, 122 kHz), 1 k y 10 nF a masa, luego 10 uF (+ hacia la línea) |
| GP25 | El LED de la placa: ver más abajo |

El gancho está descolgado por debajo de 8 V en la línea y colgado por
encima de 9 V. Después, ds_voice elimina los rebotes y distingue un colgado
corto (de 200 ms a 1,5 s) de colgar.

La cartera llega al firmador por el aire, por el micrófono del auricular: se
acerca el auricular a la cartera mientras suenan sus tonos.

### El LED

Mientras el auricular está pegado a una cartera, no se oye al firmador (y
calla: solo escucha), así que el LED dice cómo va la recepción:

| Qué pasa | El LED |
| --- | --- |
| Se oye un tono o una tecla | un destello corto |
| Entra una parte de una PSBT | un destello largo (300 ms), en la pausa entre tramas |
| Una semilla por el auricular, o una PSBT, está completa | encendido fijo, hasta una tecla o el gancho |

### El eco por USB

Solo de salida y solo datos públicos, como arriba: lo que se dice (`say`,
`ear`, `num`, `chars`), los estados, `hook on|off`, las huellas, el
descriptor, la PSBT firmada entre `signed-psbt-begin` y `signed-psbt-end`,
`work-ms N` por cada trabajo de más de 50 ms y, cada 5 s, `stack-free`,
`loop-max-us` (la vuelta más larga del bucle) y `underruns` (muestras de voz
que el PWM no encontró: entonces da silencio).

### Tiempos y memoria

Medidos a 125 MHz con la semilla de prueba (toda a ceros):

| Trabajo | Tiempo |
| --- | --- |
| La semilla, sin PIN | 2,6 s |
| La semilla, con un PIN de 12 caracteres | 4,5 s |
| La derivación de la cartera | 2,6 s |
| La cuenta | menos de 50 ms |
| El análisis de una PSBT | 170 ms |
| La firma | 130 ms |

Una tecla que pide trabajo suena con un motivo corto y luego el motivo de
espera, que la interrupción del PWM repite por su cuenta mientras el trabajo
ocupa el núcleo. Fuera de los trabajos, una vuelta del bucle tarda unos 3 ms;
la voz va 20 ms por delante (`DS_VOICE_OUT_DELAY_MS` es 40). La pila libre
mínima vista fue de 2,8 KB de 8 KB, en la revisión; la RAM estática es de
unos 129 KB de 264 KB y la flash, de unos 555 KB.

### Qué está probado (Centralita)

En una protoboard, con un teléfono de tonos que había perdido su auricular:
su gancho se apretaba a mano, unos auriculares de cable hacían de cápsula y
un módulo de micrófono electret (un MAX9814, con un divisor de
22 k / 220 ohmios y dos condensadores de 1 uF) hacía de micrófono; los tonos
de la cartera también entraron por cable a esa entrada de micrófono. Así se
probó: los menús y la lista de idiomas, el colgado corto que borra y el que
firma, la semilla por el teclado, la semilla por el auricular cifrada con un
PIN de 12 caracteres, una PSBT recibida igual, revisada y firmada byte a byte
como la versión de ordenador, la cuenta enviada y leída por un móvil con
ur-tones, el motivo de espera y el LED.

**Todavía no va bien:** por el aire, del altavoz de un portátil al módulo de
micrófono, se perdía más o menos un tono de cada diez en las tramas largas
(los portátiles dan poco por debajo de 1 kHz, donde están los tonos graves
del DTMF). Y en la placa, una cartera que seguía repitiendo una PSBT ya
completa hizo una vez que dos de sus tonos contaran como teclas, lo que
canceló la revisión; los tests de ese caso en el ordenador pasan, y se está
investigando.

## Qué está probado

Probado en el prototipo de protoboard (SH1106 por SPI): la semilla por tonos
con su PIN, las huellas, la XPUB como QR leído por Sparrow, dos PSBT firmadas
byte a byte igual que la versión para ordenador, un cambio falso rechazado y
el borrado de emergencia.

**Sin probar en hardware:** `DS_DISPLAY=SSD1306_SPI` y `SSD1306_I2C`,
otras etapas de entrada de línea y la RP2350.
