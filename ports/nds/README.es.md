[English](README.md) · Español

# DTMF-Signer en la Nintendo DS

El firmware para la consola: **escucha por el aire** (el micrófono de la DS)
y **habla por QR**. La pantalla de abajo muestra las mismas pantallas de
128 × 64 que la Pico, al doble; la de arriba muestra la PSBT firmada o la
xpub como un QR animado grande (4 partes por segundo), y la de abajo una
copia pequeña. Reutiliza `core/` y `app/` sin cambios; este directorio solo
contiene lo que los rodea (pantallas, botones, micrófono, la tapa). El
perfil es
[`PROFILE-NDS`](../../docs/es/catalog.md#profile-nds--nintendo-ds-solo-por-el-aire).
Al arrancar corta la alimentación de la radio de la DS y, en modo DSi,
mantiene en reset el chip inalámbrico de la DSi y apaga su luz; no hay
código de Wi-Fi enlazado. En modo DS, una DSi o una 3DS dejan encendida la
luz del wifi: ese modo no puede apagarla.

**Probado de momento en una consola:** una DSi XL, en modo DSi y en modo
DS, de punta a punta: una semilla con su PIN y una PSBT por el aire desde un
portátil al lado, la PSBT firmada leída por una cartera desde el QR de la
pantalla de arriba, el stylus, el borrado de emergencia y la tapa. Por el
aire aguantó el ritmo más rápido de ur-tones («Cable rápido», 30 ms de tono
+ 10 ms de silencio). Con una televisión encendida en la habitación la
semilla entró igual pero la PSBT falló; con ella apagada, la PSBT entró
también. Falta probar una DS, DS Lite o 3DS. No lo uses con fondos todavía,
y nunca pongas una semilla real en un emulador.

## Consolas

La DS y la DS Lite necesitan un flashcart de la ranura 1 que cargue archivos
`.nds`; la DSi, la DSi XL y la 3DS la ejecutan desde su cargador de homebrew,
en modo DSi por defecto (en modo DS también funciona, con la luz del wifi
encendida).

## Compilar y cargar

```bash
tools/build-nds.sh        # da build-nds/dtmf-signer-ds.nds
```

Necesita git y Docker. La compilación corre en una imagen fija de devkitARM
y es reproducible: el mismo commit da el mismo `.nds`, byte a byte. El
trabajo de CI `nds` (`.github/workflows/core.yml`) compila el mismo archivo y
lo sube como el artefacto `dtmf-signer-ds-nds`; su hash debería coincidir con
el de una compilación local del mismo commit (las de push), lo que aún no se
ha comprobado. Copia el `.nds` a tu flashcart o cargador como cualquier
homebrew.

Una prueba de extremo a extremo en emulador (melonDS):
`tools/build-nds.sh --autotest`, y luego
`tools/dev/nds_e2e.sh` con la ruta de melonDS.app (o de su binario); imprime `AUTOTEST PASS`. La
compilación de autoprueba es para desarrollo y nunca se publica.

## Controles

- **La cruceta** mueve; **A** es OK; **B** vuelve atrás (la IZQUIERDA de la
  Pico). Para confirmar una firma o terminar un selector, mantén A.
- **Lápiz:** toca la rejilla del PIN o de la frase de contraseña (una
  casilla la añade, una pestaña cambia de página) o una flecha de la fila
  de abajo ("◀ back", "▲▼ move"). OK nunca se toca: terminar y firmar
  siguen siendo mantener A. El «Tap OK, then hold OK» de la revisión se
  refiere al botón A; los toques del lápiz nunca confirman.
- **Borrado de emergencia:** mantén B 3 s, en cualquier pantalla. Todo se
  pone a cero y la consola se apaga (no se reinicia).
- **Cerrar la tapa** lo borra todo y apaga la consola. Durante un cálculo
  largo (unos segundos) surte efecto cuando termina.

El PIN y la frase de contraseña se teclean en la consola.

## Solo por el aire

La DS no tiene entrada por cable, así que la semilla le llega por el aire.
Antes de escuchar, el firmador dice: "Seed by air … Anyone who records these
tones gets the seed, or can try PINs if it is short. Use a long PIN." (el
aviso, en inglés). Es la versión solo por el aire que permiten
[la §4.2 y la §7](../../docs/es/spec.md#42-qué-se-acepta-por-dónde) de la
especificación. La PSBT y la xpub no son secretas, y el PIN y la frase de
contraseña nunca viajan por el aire.

## Tiempos (emulador)

Medidos en melonDS, modo DS: **un emulador, no una consola.** El PIN de la
semilla 5,5 s, la derivación de la cartera 2,9 s, la revisión de una PSBT
0,3 s, la firma 0,1 s, iniciar un QR 0,4 s. Nada está medido en hardware.

## Se buscan probadores

**Buscamos gente con una DS, DS Lite, DSi o 3DS real** que la pruebe: si
carga, si el micrófono oye los tonos de una cartera, si una cartera lee su
QR, si la tapa borra. Cuéntanos lo que viste en un issue del repositorio,
con el modelo de consola y cómo la cargaste. Usa solo una semilla de prueba.
