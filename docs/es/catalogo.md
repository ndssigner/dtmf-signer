[English](../catalog.md) · Español

# Catálogo de DTMF-Signer (borrador v0)

La [especificación](especificacion.md) dice qué tiene que hacer todo
DTMF-Signer. Este catálogo dice **qué añade cada pieza de un montaje, qué
necesita y cuánto cuesta**, y qué perfiles ya preparados las combinan.
Crece con cada montaje que alguien haga y mida.

## 1. Cómo leer este catálogo

- **Todas las capacidades tienen las mismas cinco líneas:** qué hace; qué
  necesita (pines, periféricos, RAM, memoria de programa); su tiempo en cada
  clase de procesador; sus riesgos; los vectores que la comprueban.
- **Clases de procesador:** `CPU-8` (8 bits: AVR, 8051, Z80, 6502, por
  debajo de 20 MHz), `CPU-32S` (32 bits modesto: Cortex-M0 y similares, por
  debajo de 50 MHz), `CPU-32` (32 bits: RP2040, RP2350 y equivalentes).
  Ver la [§9 de la especificación](especificacion.md#9-capacidades-y-perfiles).
- **Los tiempos se miden, nunca se suponen.** Mientras nadie mida uno, pone
  "sin medir".
- **Los precios**, si se dan, son rangos orientativos, o los introduce quien
  planifica un montaje. Nunca precios fijos de un país: varían demasiado.
- **Nuevo o recuperado.** Lo mejor es comprar las piezas recomendadas. Las
  piezas recuperadas son bienvenidas: el catálogo dice hasta dónde llegan y
  qué falta.

## 2. Capacidades

### `TONES-SW` — tonos por software

- **Qué hace:** decodifica DTMF a partir de muestras de audio con el
  algoritmo de Goertzel, como la librería C de ur-tones.
- **Necesita:** un ADC que muestree a 8 kHz o más, y CPU para filtrar en
  tiempo real. Un filtro antialiasing analógico antes del pin.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` en vivo, hasta 30 ms de tono + 10 ms de silencio sin errores (RP2040 a 16 kHz, 900 tonos).
- **Riesgos:** un ADC alimentado fuera de su rango se daña o recorta; la
  etapa de entrada debe protegerlo. Un procesador lento pierde tonos:
  entonces, `TONES-CHIP`.
- **Vectores:** los vectores y grabaciones de ur-tones.

### `TONES-CHIP` — tonos con un chip decodificador

- **Qué hace:** un chip decodificador DTMF (MT8870, HT9170 y compatibles)
  entrega cada tecla como cuatro bits de datos más una señal de "tono
  válido". La librería de ur-tones ya lee chips así.
- **Necesita:** cinco entradas digitales, el cristal del chip (3,579545 MHz,
  el de las teles en color) y unas pocas piezas pasivas.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** difícil de comprar nuevo en muchos sitios, pero habitual en
  teléfonos viejos, contestadores y centralitas. Un chip de origen
  desconocido decodifica todas las teclas, también las de una semilla sin
  cifrar, pero no guarda nada: no tiene memoria ni firmware.
- **Vectores:** los vectores de ur-tones, entregados como teclas.

### `IN-MIC` — micrófono

- **Qué hace:** recibe tonos por el aire (`AIR`).
- **Necesita:** una cápsula electret con amplificador, o un módulo con
  ganancia automática; un pin de ADC, o la entrada de `TONES-CHIP`.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** todo lo que recibe es `AIR`: nunca datos sensibles. No debe
  compartir pin con `IN-LINE`, salvo que el montaje trate ambos como `AIR`.
- **Vectores:** las grabaciones por el aire de ur-tones.

### `IN-LINE` — entrada de línea

- **Qué hace:** recibe tonos por cable (`CABLE`) desde una salida de
  auriculares: la vía recomendada para la semilla.
- **Necesita:** un jack o conector macho de 3,5 mm, un condensador que
  bloquee la continua, una polarización a media alimentación, una
  resistencia en serie y un filtro paso bajo; un pin de ADC, o la entrada de
  `TONES-CHIP`. Medido: decodifica desde unas 90 de 4095 cuentas pico a
  pico (con un ruido en reposo de unas 30), así que basta la salida de
  auriculares de un portátil a una cuarta parte de su volumen.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` en vivo, hasta 30 ms de tono + 10 ms de silencio sin errores (RP2040 a 16 kHz, 900 tonos).
- **Riesgos:** un cable equivocado, o una salida de altavoz, pueden sacar el
  pin de su rango: lo protegen las resistencias en serie, un condensador que
  bloquea la continua y diodos de protección. Con salidas de auriculares no
  hace falta una atenuación fija: reduciría una señal que ya llega pequeña a
  volumen bajo.
- **Vectores:** las grabaciones por cable de ur-tones.

### `IN-UART` — entrada serie

- **Qué hace:** texto o tramas de ur-tones por un puerto serie (`CABLE`),
  desde un terminal vintage u otro aparato.
- **Necesita:** una UART, y un adaptador de niveles para RS-232 (nunca
  RS-232 directo a un pin de 3,3 V).
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** el terminal ve lo que envía: los datos sensibles necesitan
  confirmación física, y mejor enviarlos desde un aparato que nunca se haya
  conectado a la red.
- **Vectores:** previstos.

### `IN-PS2` — teclado PS/2

- **Qué hace:** un teclado conectado al firmador (`LOCAL`), para el PIN, la
  passphrase y la entrada de SeedCraft en consola.
- **Necesita:** dos pines con adaptación de niveles a 5 V, y 5 V para el
  teclado.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** un teclado con firmware propio podría, en teoría, recordar
  teclas; los teclados viejos y sencillos son los más seguros.
- **Vectores:** previstos.

### `IN-PHONEPAD` — teclado de teléfono recuperado

- **Qué hace:** el teclado de un teléfono fijo, conectado por cable al
  firmador (`LOCAL`). El PIN se teclea con varias pulsaciones por tecla para
  A–Z, y pasa por `TONES-SW` o `TONES-CHIP`.
- **Necesita:** el teclado y su generador de tonos, conectados a una
  entrada de tonos.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** muchos teléfonos guardan el último número marcado, y algunos
  tienen agenda: usa solo el teclado, un teléfono sin memoria, o bórrala.
  Ver la [§4.3 de la especificación](especificacion.md#43-un-teclado-de-teléfono-recuperado).
- **Vectores:** previstos.

### `IN-BUTTONS` — botones

- **Qué hace:** la confirmación, y la navegación y escritura en una
  pantalla (`LOCAL`).
- **Necesita:** al menos un pulsador. Varios pueden compartir un pin de ADC
  con una escalera de resistencias, o usar un pin digital cada uno.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** un PIN tecleado en una rejilla en pantalla puede
  reconstruirlo quien vea la pantalla.
- **Vectores:** previstos.

### `OUT-AIR` — tonos por el aire

- **Qué hace:** devuelve datos (PSBT, XPUB, resúmenes, huellas) como tonos
  por un altavoz, un auricular o un piezo (`AIR`).
- **Necesita:** un pin de PWM. **Un auricular de cable o un altavoz pequeño
  funcionan**, también recuperados: probado, un auricular junto al micrófono
  de un móvil dio todas las teclas (pin, 1 kΩ, 10 µF, auricular). Un piezo de
  disco puede excitarse directamente desde dos pines. **Un zumbador
  magnético (2 kHz) no sirve** para DTMF: suena mucho más fuerte en el grupo
  agudo, y un decodificador rechaza la mayoría de teclas aun con el grupo
  grave 12 dB más fuerte. Sirve para pitidos.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** cualquiera cerca lo oye: nunca datos sensibles (§4 de la
  especificación), aunque el auricular esté pegado al micrófono.
- **Vectores:** los vectores de ur-tones.

### `OUT-LINE` — salida de línea

- **Qué hace:** devuelve tonos por cable (`CABLE`).
- **Necesita:** un pin de PWM y un filtro RC, hasta un jack.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** ninguno aparte de los del aparato que recibe.
- **Vectores:** los vectores de ur-tones.

### `OUT-UART` — salida serie

- **Qué hace:** devuelve texto o tramas por un puerto serie (`CABLE`).
- **Necesita:** la UART de `IN-UART`.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** nunca un eco del PIN ni de la passphrase (`INV-2`).
- **Vectores:** previstos.

### `OUT-QR` — códigos QR en la pantalla

- **Qué hace:** devuelve datos (una PSBT firmada, una XPUB, un descriptor)
  como códigos QR en la pantalla del firmador (`AIR`): fijos, o URs animados
  para datos largos, como los leen carteras como Sparrow. Con `IN-LINE`, un
  montaje puede escuchar por cable y hablar por QR, sin altavoz ni micrófono.
- **Necesita:** una pantalla (`VERIFY-SCREEN`) y un generador de QR (los hay
  pequeños en C, sin malloc). La imagen invertida (fondo claro, módulos
  oscuros, como en papel) y el contraste ajustable, bajo por defecto. Medido
  en una OLED de 128 × 64: cabe hasta un QR versión 7 (45 módulos) a 1 píxel
  por módulo, suficiente para un descriptor BIP-84 (146 caracteres) en un
  solo código. La cámara de un móvil leyó todos los códigos a 1 píxel por
  módulo; la webcam de un portátil, con Sparrow, se deslumbraba con el
  contraste al máximo y leyó el descriptor al bajarlo. Las pantallas mayores
  (128 × 128) dan para más en cada cuadro.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** cualquier cámara que vea la pantalla puede leerlo: nunca datos
  sensibles (§4 de la especificación). No sirve como verificación: el
  aparato que lo lee es justo aquel del que no nos fiamos. Una cartera
  necesita un descriptor, con la huella y la ruta, para importar una cuenta:
  no basta una XPUB suelta.
- **Vectores:** previstos (los textos que muestra salen de los vectores del
  núcleo).

### `VERIFY-SCREEN` — la pantalla del propio firmador

- **Qué hace:** muestra el resumen, las huellas y las preguntas. Es la
  verificación recomendada.
- **Necesita:** una OLED (SH1106, SSD1306, SSD1315), una LCD de caracteres
  HD44780 (habitual en impresoras, faxes y centralitas de alarma viejas) o
  displays de 7 segmentos; pines I2C, SPI o en paralelo.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** un implante que lea la pantalla ve lo que muestra; la
  semilla nunca se muestra, así que solo puede capturar las rejillas del PIN
  y la passphrase. Sí ve los resúmenes, la XPUB y las huellas: una fuga de
  privacidad, aunque no un robo.
- **Vectores:** previstos (el texto del resumen).

### `VERIFY-TERMINAL` — un terminal

- **Qué hace:** el resumen en un terminal vintage por UART.
- **Necesita:** `IN-UART` y `OUT-UART`.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** el terminal debe ser uno que controle el firmador, no el
  aparato que creó la PSBT. La confirmación final sigue siendo en el
  firmador.
- **Vectores:** previstos.

### `VERIFY-TV` — una tele

- **Qué hace:** el resumen en una tele, como vídeo compuesto en blanco y
  negro.
- **Necesita:** dos pines y dos resistencias hasta un conector RCA, y la CPU
  o un periférico para generar la señal.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** las smart TV pueden analizar y enviar lo que muestran; el
  vídeo analógico puede captarse a distancia con equipo especializado.
- **Vectores:** previstos.

### `VERIFY-LISTENERS` — varios escuchadores

- **Qué hace:** el resumen se emite por el aire y lo muestran varios
  escuchadores de ur-tones independientes. Es correcto si al menos uno es
  honesto. Ver la [§5.1 de la especificación](especificacion.md#51-varios-escuchadores).
- **Necesita:** `OUT-AIR`, y dos o más escuchadores independientes (por
  ejemplo, la herramienta web de ur-tones en un móvil y en un ordenador, sin
  conexión).
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** escuchadores que no sean independientes de verdad; tonos
  inyectados por un altavoz cercano (el código de comprobación está
  pendiente).
- **Vectores:** previstos.

### `STORE-SEED` — guardar la semilla

- **Qué hace:** guarda la semilla, cifrada, aunque se apague.
- **Necesita:** flash o EEPROM, y un borrado de emergencia que se active
  físicamente (combinación de botones, interruptor o puente).
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** memoria de procedencia dudosa, extracción de un aparato
  robado, un borrado del que no hay garantías (nivelación de desgaste). Ver
  la [§8 de la especificación](especificacion.md#8-guardar-la-semilla-y-borrado-de-emergencia).
- **Vectores:** previstos.

### `BTC-TAPROOT` — Taproot

- **Qué hace:** P2TR de firma única: firmas Schnorr y el hash de firma de
  BIP341.
- **Necesita:** más código y más cálculo de hashes que P2WPKH.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** más código que auditar.
- **Vectores:** previstos.

### `BTC-MULTISIG` — multifirma

- **Qué hace:** firma como uno de varios firmantes.
- **Necesita:** conocer y comprobar las claves públicas de los demás
  firmantes, para no aceptar un cambio falso; más memoria.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** mucho más difícil de verificar correctamente.
- **Vectores:** previstos.

### `BTC-PATHS` — otras cuentas y rutas

- **Qué hace:** cuentas y rutas de derivación más allá de la cuenta BIP84
  del núcleo.
- **Necesita:** una forma de elegirlas y de mostrarlas.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** una ruta equivocada da una cartera vacía; tiene que verse.
- **Vectores:** previstos.

### `BTC-INPUT-AMOUNTS` — importes comprobados con varias entradas

- **Qué hace:** saca el importe de cada entrada de su transacción anterior
  completa (`non_witness_utxo`), contra el ataque de la comisión en SegWit
  v0. Obligatoria en `PROFILE-PICO-REF`.
- **Necesita:** calcular el hash de cada transacción anterior mientras se
  lee por partes; recibir más datos (una transacción anterior suele ocupar
  unos cientos de bytes, pero puede llegar a decenas de kilobytes).
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** sin ella, el firmador muestra "comisión NO verificada" y el
  importe declarado de cada entrada, y el usuario los comprueba en otro
  sitio antes de confirmar. Ver la
  [§6 de la especificación](especificacion.md#6-el-ataque-de-la-comisión-en-segwit-v0).
- **Vectores:** previstos.

### `SPLIT-MCU` — varios microcontroladores

- **Qué hace:** un microcontrolador se encarga de la entrada y la salida, y
  otro guarda la semilla y firma. Se comunican por serie con tramas de
  ur-tones (`ut_frame`), sin audio. La separación aísla código y memoria,
  no secretos.
- **Necesita:** dos microcontroladores y un enlace serie entre ellos.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** el protocolo interno debe ser tan estricto como la política
  de canales. El lado de entrada y salida maneja todo lo que se introduce
  (el PIN, la passphrase, la semilla de camino), así que hay que confiar en
  él como en el lado que firma.
- **Vectores:** previstos.

### `SEEDCRAFT-CONSOLE` — entrada de SeedCraft en consola

- **Qué hace:** una semilla guardada con [SeedCraft](https://github.com/ndssigner/seedcraft)
  se teclea con un teclado PS/2 y una tele, o en un terminal, a ciegas: solo
  se muestra el progreso, nunca lo tecleado.
- **Necesita:** `IN-PS2` y `VERIFY-TV`, o `VERIFY-TERMINAL`.
- **Tiempo:** `CPU-8` sin medir · `CPU-32S` sin medir · `CPU-32` sin medir.
- **Riesgos:** por UART el terminal ve la semilla entera: mejor un aparato
  que nunca se haya conectado a la red. Cuenta como semilla sin cifrar en la
  política de canales.
- **Vectores:** previstos.

## 3. Perfiles

### `PROFILE-SCRAP` — Chatarra mínima

- **Capacidades:** `TONES-CHIP`, `IN-LINE`, `IN-BUTTONS` (un pulsador) o
  `IN-PHONEPAD`, `OUT-AIR`, `VERIFY-LISTENERS`.
- **Piezas:** un microcontrolador de 8 bits, un MT8870 recuperado, un jack
  de entrada, un pulsador, un zumbador.
- **Qué le falta:** pantalla propia, `STORE-SEED`, `BTC-TAPROOT`,
  `BTC-MULTISIG`; quizá `BTC-INPUT-AMOUNTS`, y entonces los importes de las
  entradas se comprueban en otro sitio antes de confirmar. Firmar es lento.

### `PROFILE-PICO-REF` — Referencia Pico

- **Capacidades:** `TONES-SW`, `IN-LINE`, `IN-MIC`, `IN-BUTTONS`,
  `OUT-AIR`, `OUT-QR`, `VERIFY-SCREEN`, `BTC-INPUT-AMOUNTS`.
- **Piezas:** el prototipo actual, en protoboard: una placa RP2040 con la
  disposición de la Pico y USB-C (la RP2350 más adelante), una OLED SH1106
  de 1,3" por SPI (GP17 a GP21), pulsadores, un conector macho de 3,5 mm como
  entrada de línea (GP27), un auricular de cable como salida por el aire
  (GP15) y un módulo de micrófono MAX9814 (GP26, aún sin probar).
- **Qué le falta:** `STORE-SEED`, `BTC-TAPROOT`, `BTC-MULTISIG`, `IN-UART`
  y los demás puertos, de momento.

### `PROFILE-PICO-FULL` — Pico completo

- **Capacidades:** las de `PROFILE-PICO-REF`, más `IN-UART`, `OUT-UART`,
  `VERIFY-TERMINAL`, `IN-PS2`, `VERIFY-TV`, `OUT-LINE` y, opcionalmente,
  `SEEDCRAFT-CONSOLE` y `BTC-TAPROOT` (previsto en sus módulos de firmware).
  Sin `STORE-SEED`: esta placa no guarda estado. Una
  variante que lo añada necesita el borrado de emergencia de la §8 de la
  especificación.
- **Piezas:** la placa de [`docs/hardware-minimo.md`](../hardware-minimo.md).
