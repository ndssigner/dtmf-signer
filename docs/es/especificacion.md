[English](../../SPEC.md) · Español

# Especificación de DTMF-Signer (borrador v0)

**Estado: borrador para discusión.** Nada es definitivo, todavía no hay
ningún aparato que funcione y nadie independiente lo ha revisado. No lo
uses con fondos.

DTMF-Signer es un firmador de Bitcoin que se comunica con tonos de teléfono
(DTMF) mediante [ur-tones](https://github.com/ndssigner/ur-tones). Este
documento dice **qué tiene que hacer cualquier DTMF-Signer, esté hecho con
lo que esté hecho**: una Raspberry Pi Pico, un microcontrolador de 8 bits,
un procesador vintage, un chip decodificador DTMF recuperado de un teléfono
viejo. Cómo lo hace cada montaje se describe en otros documentos: el
[catálogo](catalogo.md) enumera las capacidades y los
perfiles que las combinan, y
[`docs/hardware-minimo.md`](../hardware-minimo.md) describe la placa de la
Pico.

- **El objetivo:** que cualquiera pueda construir un firmador de confianza,
  con piezas nuevas o, en el peor caso, con piezas recuperadas de una
  chatarrería, sabiendo exactamente qué pierde cada montaje y cuánto tarda.
- **La especificación y sus vectores de prueba son el producto.** El
  firmware de referencia es una implementación más. Un port puede ser una
  reimplementación, incluso en ensamblador, guiada por este documento y
  comprobada con los vectores (§10).
- **ur-tones:** los tonos, las tramas, el modo teclado y el PIN de la
  semilla siguen la versión `0` del formato ur-tones (su `SPEC.md`, versión
  `v0.3.0` o posterior). Lo que DTMF-Signer necesite de ur-tones se propone
  allí, en público, con issues y pull requests, según sus propias reglas.

Las palabras DEBE, NO DEBE, OBLIGATORIO (u OBLIGATORIA), DEBERÍA,
RECOMENDADO (o RECOMENDADA) y PUEDE se usan como MUST, MUST NOT, REQUIRED,
SHOULD, RECOMMENDED y MAY en el RFC 2119.

## 1. Principios

1. **Primero la especificación y los vectores.** Un montaje es conforme
   cuando hace lo que dice este documento y reproduce los vectores.
2. **El firmador nunca calla lo que no ha comprobado.** Un montaje comprueba
   en el firmador todo lo que se puede permitir. Lo que no pueda comprobar,
   DEBE decirlo por su propia salida (por ejemplo, "comisión NO
   verificada"), y el catálogo indica qué comprobación externa la sustituye.
   Esa comprobación pasa entonces al usuario, sabiéndolo.
3. **Los tiempos se publican medidos.** Nadie debe llevarse sorpresas con un
   procesador lento. Un tiempo que no se ha medido se escribe "sin medir".
4. **Sin estado por defecto.** La semilla vive solo en RAM. Guardarla es una
   capacidad opcional, con sus riesgos explicados (§8).

## 2. Operaciones del núcleo

Todo montaje DEBE ofrecer todas estas.

| ID | Operación | Detalle |
|---|---|---|
| `OP-SEED` | Cargar la semilla | En tonos de ur-tones, cifrada con PIN o sin cifrar. Solo en RAM. |
| `OP-PASSPHRASE` | Recibir una passphrase | El firmador DEBE aceptarla; usarla es decisión del usuario. Solo ASCII. |
| `OP-FINGERPRINT` | Presentar las huellas | Por la salida de verificación (§5). La de la semilla, tras cargarla (como la define ur-tones); la de la cartera final, tras la passphrase. |
| `OP-XPUB` | Devolver la XPUB | De la cuenta BIP84 `m/84'/0'/0'` (y su equivalente en redes de prueba, `m/84'/1'/0'`), con la huella de la clave maestra. El descriptor es `wpkh(@0)`, con el origen escrito con apóstrofos, como en `/84'/0'/0'`. |
| `OP-SIGN` | Firmar una PSBT P2WPKH | Procesada por partes, nunca entera en RAM (§2.1). |
| `OP-WIPE` | Borrar | RAM a cero al terminar, al cancelar y con el borrado de emergencia (§8). |
| `OP-TESTNET` | Redes de prueba | testnet y signet, para practicar sin dinero real. |

**Tipos de dirección.** Solo es obligatoria **P2WPKH de firma única**
(BIP84, ECDSA). Taproot y multifirma son capacidades opcionales
(`BTC-TAPROOT`, `BTC-MULTISIG`). El catálogo dice claramente qué montajes no
pueden llegar a ellas, y un montaje sin ellas puede usar Bitcoin igualmente.

### 2.1 Firma

1. La PSBT se lee **por partes**, para que un montaje con poca RAM pueda
   firmar una transacción con varias entradas.
2. El firmador identifica **él mismo el cambio**: una salida es cambio solo
   si su ruta de derivación y su clave son las del propio firmador
   (`INV-5`).
3. Calcula la comisión (§6) y presenta un **resumen** —importes, destinos,
   comisión, cuál es el cambio— por una salida de verificación que controla
   él (§5).
4. Espera la **confirmación física** en el firmador.
5. Firma solo con `SIGHASH_ALL`, con firmas deterministas (RFC 6979: no
   hace falta generador de números aleatorios), y devuelve la PSBT.

## 3. Invariantes

Un montaje conforme NO DEBE romper ninguno de estos, por ningún canal y en
ningún estado.

1. `INV-1` — La semilla nunca sale del firmador y nunca se muestra. Solo su
   huella.
2. `INV-2` — El PIN y la passphrase nunca salen del firmador, ni siquiera
   como eco.
3. `INV-3` — No se firma nada sin confirmación física en el firmador.
4. `INV-4` — No se firma nada antes de haber presentado su resumen por una
   salida de verificación que controla el firmador (§5).
5. `INV-5` — Ninguna salida se trata como cambio sin que el firmador haya
   comprobado que es suya.
6. `INV-6` — No se presenta como verificado nada que el firmador no haya
   verificado (principio 2).

## 4. Canales

### 4.1 Clases

Los canales se agrupan por **lo que exponen**, no por puerto.

| Clase | Qué es | Ejemplos |
|---|---|---|
| `AIR` | Aire, difusión: cualquiera cerca puede oír, o ver | Micrófono, zumbador, altavoz, un QR en la pantalla del firmador |
| `CABLE` | Punto a punto con otro aparato, que conoce lo que envía | Jack de entrada de línea, UART, salida de línea |
| `LOCAL` | Parte del firmador, sin otro aparato detrás que conozca el dato | Botones, teclado PS/2, un teclado de teléfono recuperado (§4.3), la pantalla del propio firmador |

Todo dato DEBE llegar al núcleo **etiquetado con su clase**. Si un montaje
no puede distinguir sus fuentes (por ejemplo, varias entradas en un mismo
pin o en un mismo chip decodificador), DEBE etiquetar sus datos con la clase
más restrictiva entre ellas, en el orden `AIR`, `CABLE`, `LOCAL`: `AIR` si
entre ellas hay un micrófono. Un teclado de teléfono conectado a la entrada
de línea es entonces `CABLE`, que sigue aceptando el PIN y la passphrase
(§4.2).

**La velocidad no es una clase.** Un auricular pegado a un micrófono lleva
los tonos rápido y limpios, pero sigue siendo `AIR`: cualquiera que esté lo
bastante cerca puede grabarlo.

**Un QR también es `AIR`.** La pantalla del firmador es `LOCAL` cuando le
muestra texto al usuario, pero un QR en ella es una salida que puede leer
cualquier cámara que la vea.

### 4.2 Qué se acepta por dónde

| Dato | `AIR` | `CABLE` | `LOCAL` |
|---|---|---|---|
| PSBT, XPUB, resúmenes, huellas | ✅ | ✅ | — |
| Semilla cifrada con PIN | ⚠️ con aviso, y un PIN de al menos `12` caracteres | ✅ recomendado | ❌ (obligaría a mostrarla) |
| Semilla sin cifrar | ❌ | ✅ | ❌ salvo la entrada de SeedCraft en consola, a ciegas |
| PIN | ❌ | ✅ | ✅ |
| Passphrase | ❌ | ✅ | ✅ |

- **Los datos sensibles (la semilla sin cifrar, el PIN, la passphrase) van
  solo por `CABLE` o `LOCAL`, aunque viajen en DTMF.**
- **Los datos sensibles por UART solo se aceptan con confirmación física**:
  el firmador pregunta, por ejemplo, "¿Aceptar passphrase recibida por
  UART?". Un terminal conectado nunca puede meter datos sensibles sin que el
  usuario lo sepa.
- **El cable protege el camino, no el origen.** El aparato que envía el
  dato lo conoce. Lo mejor es enviar los datos sensibles desde un aparato
  que nunca se haya conectado a la red: un móvil viejo en modo avión, un
  reproductor de audio, un ordenador vintage.
- **Las grabaciones son copias.** Un archivo de audio con la semilla o la
  passphrase merece el mismo cuidado que una copia en papel; si se guarda,
  cifrado con un PIN largo.
- **Por cable y en directo, la passphrase puede ir en claro.** Cifrarla
  solo tiene sentido en grabaciones guardadas, con una sal propia (por
  ejemplo `ur-tones/passphrase-pin/v0`) y rellenada hasta una longitud fija.
  Aun así, un PIN equivocado da un texto que no parece texto, lo que permite
  al atacante comprobar rápido si acertó: una passphrase cifrada guardada
  junto a la semilla cifrada debilita a las dos. *Pendiente en la
  especificación de ur-tones (§12).*

### 4.3 Un teclado de teléfono recuperado

Un teclado DTMF de teléfono fijo, conectado por cable al firmador, es
`LOCAL`: no hay otro aparato detrás. El PIN se teclea como en los móviles
antiguos, con varias pulsaciones por tecla para A–Z, y pasa por la misma
decodificación de tonos que la semilla (por software o con un chip
decodificador). Con un teclado así, el mínimo es un solo botón de
confirmación, o una tecla del propio teclado.

**Aviso:** muchos teléfonos guardan el último número marcado, y algunos
tienen agenda: el PIN se quedaría en el teléfono. Usa solo el teclado, un
teléfono sin memoria, o bórrala marcando otra cosa.

## 5. Verificar lo que se firma

Si el firmador no muestra él mismo los importes y los destinos, el
ordenador conectado podría mentir sobre la transacción y el usuario
firmaría a ciegas. Por eso el requisito es: **el resumen sale del firmador
por un camino que el aparato conectado no controla.** Formas admitidas:

- **La pantalla del propio firmador** (RECOMENDADA): OLED, una LCD de
  caracteres HD44780, displays de 7 segmentos.
- **Un terminal por UART, o una tele** controlada por el firmador.
- **Varios escuchadores por el aire** (§5.1).
- **Una lectura en tonos o pitidos:** último recurso, lento y propenso a
  errores.

Lo que nunca vale es la presentación de la transacción que hace el propio
aparato conectado. Tampoco un QR (`OUT-QR`): el aparato que lo lee es justo
aquel del que no nos fiamos.

### 5.1 Varios escuchadores

El aire es difusión: todos los escuchadores oyen la misma emisión a la vez.
El firmador emite el resumen (importes, destinos, comisión, cuál es el
cambio) y varios escuchadores compatibles con ur-tones lo decodifican y lo
muestran.

- **La garantía:** si N escuchadores **independientes** muestran lo mismo,
  es correcto siempre que **al menos uno sea honesto**. Un atacante que
  controle uno provoca una discrepancia, que lo delata; para engañar al
  usuario tendría que controlarlos todos.
- **Independientes de verdad:** software o hardware distinto, por ejemplo
  un móvil y un ordenador, sin conexión. La misma app en dos móviles con el
  mismo sistema puede fallar, o estar comprometida, a la vez. El aparato que
  creó la PSBT puede ser uno de los escuchadores, pero no cuenta como el
  honesto.
- **La herramienta web de ur-tones vale como escuchador**, comprobada con
  su SHA-256. El hash DEBE llegar por una vía distinta de la descarga: una
  etiqueta firmada del repositorio, las notas de la versión, o varias
  fuentes a la vez. El hash comprueba la herramienta, no el navegador ni el
  sistema: la misma herramienta en dos navegadores del mismo portátil no son
  dos escuchadores independientes.
- **Tonos inyectados:** un altavoz cercano podría emitir tonos falsos
  mientras emite el firmador. El resumen llevará un código corto que el
  firmador también presenta por su propia vía (LEDs, pitidos) y que los
  escuchadores muestran. *Detalle pendiente (§12).*

Con esto, un montaje sin ninguna pantalla puede verificar lo que firma.
Su propia pantalla sigue siendo lo recomendado.

## 6. El ataque de la comisión en SegWit v0

En SegWit v0 (BIP143) cada firma cubre el importe de **su propia** entrada,
no el de las demás. Con dos entradas o más, un ordenador malicioso puede
mentir sobre los importes en dos sesiones de firma distintas: el usuario ve
una comisión normal y acaba firmando una enorme.

- **Una entrada:** basta el importe declarado (`witness_utxo`). Una mentira
  solo produce una firma inválida.
- **`2` entradas o más:** es OBLIGATORIA la transacción anterior completa
  (`non_witness_utxo`) de cada entrada. El firmador calcula su hash mientras
  la lee por partes, comprueba que coincide con la entrada que se gasta y
  saca el importe de ahí.
- Un montaje que no pueda permitírselo DEBE declarar ausente la capacidad
  `BTC-INPUT-AMOUNTS`. Entonces muestra "comisión NO verificada" (`INV-6`)
  y, para cada entrada, la entrada que se gasta y su importe declarado.
  **Antes de confirmar**, el usuario comprueba esos importes con otro
  aparato o programa distinto del que creó la PSBT: la herramienta web de
  ur-tones, una cartera en otro aparato, su propio nodo. Comprobarlo después
  de firmar es tarde: la PSBT firmada vuelve al aparato que la creó, y uno
  malicioso puede transmitir la transacción él mismo.
- Dentro de una sesión, un montaje así DEBERÍA recordar las entradas que ha
  firmado y avisar si se le pide firmar alguna de nuevo: una segunda
  petición para las mismas entradas es justo como funciona el ataque.

## 7. Hardware mínimo

De las §2–§5 se deduce que todo montaje necesita al menos:

- **Una entrada `CABLE`** (jack de entrada de línea o UART): la vía
  recomendada para la semilla cifrada, la única para la semilla sin cifrar,
  y válida para el PIN y la passphrase si no hay entrada `LOCAL`.
- **Un botón de confirmación**, o una tecla de un teclado `LOCAL`.
- **Una salida para devolver datos:** tonos por el aire, salida de línea o
  UART.
- **Una salida de verificación** de las de la §5.

**Un ejemplo del suelo:** un microcontrolador, un jack de entrada (con un
chip decodificador MT8870 si el procesador es lento), un pulsador y un
auricular o un altavoz pequeño recuperados; verificación con dos
escuchadores independientes; el PIN con un teclado de teléfono recuperado o
por cable. O, en lugar del auricular y los escuchadores, una tele vieja:
muestra el resumen, y la PSBT firmada como un QR. Es el perfil
`PROFILE-SCRAP`.

## 8. Guardar la semilla y borrado de emergencia

- **Sin estado es lo predeterminado.** La grabación cifrada de la semilla
  es a la vez la copia de seguridad y la forma de cargarla.
- **Guardar la semilla es opcional** (`STORE-SEED`), cifrada, con sus
  riesgos explicados: memoria de procedencia dudosa, extracción de un
  aparato robado, un borrado del que no hay garantías.
- **Un montaje que guarda la semilla DEBE tener borrado de emergencia:**
  - se activa físicamente y funciona sin pantalla: una combinación de
    botones mantenida unos segundos, o un interruptor o puente dedicado;
  - sobrescribe la semilla y comprueba después que se lee a cero. No se
    limita a marcarla como libre. Cuidado con las memorias que reparten las
    escrituras por su cuenta (nivelación de desgaste).
- **Sin estado:** borrar es poner la RAM a cero. Quitar la corriente no
  garantiza un borrado instantáneo.

## 9. Capacidades y perfiles

Un montaje se describe como **el núcleo (§2–§8) más un conjunto de
capacidades**. Los perfiles son conjuntos de capacidades ya preparados y
con nombre, para facilitar la elección. El [catálogo](catalogo.md) describe cada una en
detalle.

**Todas las capacidades se describen igual:** qué hace; qué necesita
(pines, periféricos, RAM, memoria de programa); su tiempo en cada clase de
procesador; sus riesgos; los vectores que la comprueban.

**Clases de procesador**, para dar tiempos sin atarlos a un chip:

| ID | Clase |
|---|---|
| `CPU-8` | 8 bits: AVR, 8051, Z80, 6502, por debajo de 20 MHz |
| `CPU-32S` | 32 bits modesto: Cortex-M0 y similares, por debajo de 50 MHz |
| `CPU-32` | 32 bits: RP2040, RP2350 y equivalentes |

**Capacidades:**

| Grupo | IDs |
|---|---|
| Entrada de tonos | `TONES-SW` (por software), `TONES-CHIP` (chip decodificador) |
| Canales de entrada | `IN-MIC`, `IN-LINE`, `IN-UART`, `IN-PS2`, `IN-PHONEPAD`, `IN-BUTTONS` |
| Salidas de datos | `OUT-AIR`, `OUT-LINE`, `OUT-UART`, `OUT-QR` |
| Verificación | `VERIFY-SCREEN`, `VERIFY-TERMINAL`, `VERIFY-TV`, `VERIFY-LISTENERS` |
| Almacenamiento | `STORE-SEED` |
| Bitcoin opcional | `BTC-TAPROOT`, `BTC-MULTISIG`, `BTC-PATHS`, `BTC-INPUT-AMOUNTS` |
| Varios microcontroladores | `SPLIT-MCU` |
| Otras | `SEEDCRAFT-CONSOLE` |

**Perfiles:**

- `PROFILE-SCRAP` — **Chatarra mínima:** un microcontrolador de 8 bits, un
  MT8870 recuperado, un jack de entrada, un pulsador y un auricular
  recuperado; varios escuchadores, o una tele vieja con códigos QR; sin
  almacenamiento.
- `PROFILE-CABLE-QR` — **Cable y QR:** escucha por cable y habla por QR: un
  jack de entrada, unos pocos botones y una pantalla gráfica, sin ningún
  camino por el aire.
- `PROFILE-PICO-REF` — **Referencia Pico:** el prototipo actual, sobre una
  Raspberry Pi Pico.
- `PROFILE-PICO-FULL` — **Pico completo:** la placa de
  [`docs/hardware-minimo.md`](../hardware-minimo.md).

## 10. Vectores de prueba

Los vectores de prueba están en `vectors/`, en JSON, como los de ur-tones.
**Solo semillas de prueba públicas y PSBT de testnet.**

- **Hecho:** `vectors/keys-v0.json` tiene las huellas de la semilla y de la
  cartera y la XPUB BIP84 de semillas de prueba públicas. Sus huellas de la
  semilla están comprobadas con el JavaScript de ur-tones, una
  implementación independiente del núcleo en C
  (`tests/check_fingerprints.mjs`).
- **Hecho:** `vectors/psbt-v0.json` tiene vectores de PSBT versión 0: `13`
  que un montaje DEBE firmar, con los importes, las direcciones, el cambio,
  la comisión y las firmas esperados, y `10` que DEBE rechazar, con el error
  esperado. Están generados con embit, una implementación independiente del
  núcleo en C (`tools/gen_psbt_vectors.py`, cuya dependencia está fijada en
  `tools/requirements-vectors.txt`).
- **Previsto, para cada operación del núcleo:** descifrar la semilla con un
  PIN; de BIP39 a semilla con passphrase.
- **PSBT malintencionadas**, que un montaje DEBE rechazar o marcar. Cubiertas
  por `vectors/psbt-v0.json`: un cambio falso (nuestra ruta, la clave de
  otro), un cambio a otro tipo de dirección (P2SH-P2WPKH), la red
  equivocada, un sighash distinto de `SIGHASH_ALL`, una comisión
  desproporcionada (se marca, no se rechaza, por encima del `10%` de las
  salidas externas o de `1000000` sat), una clave duplicada, importes falsos
  con `2` entradas o más y sin `non_witness_utxo` (también un txid o un
  importe erróneo en él) y una PSBT de versión 2. Aún sin cubrir: otros
  campos mal formados (mapas truncados, longitudes que se pasan, una
  transacción mal formada).

## 11. Consideraciones de seguridad

- Las reglas centrales son los invariantes (§3) y la política de canales
  (§4); el resto del documento se deduce de ellas.
- No se supone ningún elemento seguro, y un montaje puede estar hecho de
  piezas de origen desconocido. El funcionamiento sin estado (§8) es lo que
  impide que un aparato robado o dudoso guarde algo que merezca la pena
  extraer.
- Lo que un montaje no pueda comprobar, lo dice (principio 2), y el
  catálogo indica la comprobación externa.
- El análisis de riesgos completo, incluidos la cadena de suministro y la
  privacidad del comprador, está en
  [`docs/analisis-riesgos.md`](../analisis-riesgos.md).

## 12. Preguntas abiertas

- **Un código de comprobación contra tonos inyectados** al verificar con
  varios escuchadores (§5.1).
- **Un tipo de trama "resumen de verificación" en ur-tones**, que se
  propondrá allí con un issue y un pull request.
- **La sal y el relleno de una passphrase cifrada** en ur-tones (§4.2).
- **La librería C de ur-tones vista desde 8 bits:** si compila con
  compiladores como SDCC o cc65, que no tiene enteros de 64 bits, y si
  necesita coma flotante en algún punto.
- **Un ritmo más rápido por cable, o por el aire de cerca**, medido con un
  patrón de entrenamiento, y menos paridad a ese ritmo: propuesto en
  [ur-tones#1](https://github.com/ndssigner/ur-tones/issues/1).
- **El formato de datos del catálogo**, cuando alguna herramienta necesite
  leerlo.
