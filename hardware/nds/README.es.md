[English](README.md) · Español

# Nintendo DS

Un DTMF-Signer sin nada que montar: la consola es el hardware. Su micrófono
oye los tonos de tu cartera, sus dos pantallas y sus botones son la
interfaz, y la transacción firmada sale como un QR animado para la cámara de
tu cartera. El perfil es
[`PROFILE-NDS`](../../docs/es/catalog.md#profile-nds--nintendo-ds-solo-por-el-aire);
el firmware, cómo compilarlo, cargarlo y usarlo:
[`ports/nds/`](../../ports/nds/README.es.md).

**Solo por el aire:** la DS no tiene más entrada que su micrófono, así que
la semilla le llega por la habitación, tras un aviso (§4.2 y §7 de la
[especificación](../../docs/es/spec.md#42-qué-se-acepta-por-dónde)). Usa un
PIN largo.

## Qué consolas

- **DS y DS Lite:** un flashcart de la ranura 1 que cargue archivos `.nds`.
- **DSi, DSi XL y 3DS:** su cargador de homebrew la ejecuta, en modo DSi por
  defecto; sin flashcart.

**Estado:** probado en una DSi XL (una semilla por el aire, el stylus, el
borrado, la tapa); falta probar la firma de una PSBT y las demás consolas:
[se buscan probadores](../../ports/nds/README.es.md#se-buscan-probadores).
No lo uses con fondos.

No hay lista de piezas, esquema ni disposición, y no hay que soldar.
