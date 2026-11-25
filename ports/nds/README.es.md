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
La radio de la DS nunca se inicializa y no hay código de Wi-Fi enlazado.

**Aún sin probar en una consola real.** Funciona en un emulador, donde pasa
la autoprueba. No lo uses con fondos, y nunca pongas una semilla real en un
emulador.

## Consolas

La DS y la DS Lite necesitan un flashcart de la ranura 1 que cargue archivos
`.nds`; la DSi, la DSi XL y la 3DS la ejecutan desde su cargador de homebrew,
en modo DS.

## Compilar y cargar

```bash
tools/build-nds.sh        # da build-nds/dtmf-signer-ds.nds
```

Necesita git y Docker. La compilación corre en una imagen fija de devkitARM
y es reproducible: el mismo commit da el mismo `.nds`, byte a byte. El
trabajo de CI `nds` (`.github/workflows/core.yml`) también lo compila y lo
sube como el artefacto `dtmf-signer-ds-nds`. Copia el `.nds` a tu flashcart
o cargador como cualquier homebrew.

Una prueba de extremo a extremo en emulador (macOS, melonDS):
`tools/build-nds.sh --autotest`, y luego
`tools/dev/nds_e2e.sh` con la ruta de melonDS.app; imprime `AUTOTEST PASS`. La
compilación de autoprueba es para desarrollo y nunca se publica.

## Controles

- **La cruceta** mueve; **A** es OK; **B** vuelve atrás (la IZQUIERDA de la
  Pico). Para confirmar una firma o terminar un selector, mantén A.
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
