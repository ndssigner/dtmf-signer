**La consola es todo el firmador.** Nada que montar: una Nintendo DS (o una
DSi, o una 3DS) carga el firmware como cualquier homebrew. Su micrófono oye
los tonos de tu wallet, sus pantallas y botones son la interfaz, y la
transacción firmada vuelve como un QR animado para la cámara de tu wallet.

No es [NDS-Signer](https://github.com/ndssigner/nds-signer), SeedSigner en
una DSi: este funciona en todas las DS, también la original y la Lite, con
su propio código pequeño en C ([las diferencias](https://github.com/ndssigner/dtmf-signer/blob/main/ports/nds/README.es.md#dtmf-signer-o-nds-signer)).

Los tonos hablan [ur-tones](https://github.com/ndssigner/ur-tones/blob/main/SPEC.md), que puedes oír y decodificar con su
[herramienta web](https://ndssigner.github.io/ur-tones/).

## Hace

- Recibe tu semilla por el aire, tras un aviso, con el PIN tecleado en la
  consola; nunca muestra la semilla: solo su huella.
- Muestra lo que gasta una transacción y adónde la envía, y firma solo
  cuando mantienes A.
- Lo borra todo al cerrar la tapa o al mantener B 3 segundos.

## No hace

- Guardar nada: al apagarla, la semilla desaparece.
- Recibir tonos por cable: solo por el aire, así que usa un PIN largo.
- Taproot ni multifirma, todavía.
- Estar probado en todas las consolas, todavía: firma de punta a punta en
  una DSi XL, y se buscan probadores para las demás.
