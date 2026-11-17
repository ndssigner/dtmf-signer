**Un teléfono de tonos es toda la interfaz.** El firmador alimenta la línea
del teléfono, como una centralita. Tecleas en el teclado; te habla por el
auricular, te lee qué gasta una transacción y adónde lo envía, y solo firma
cuando cuelgas un momento y vuelves a descolgar.

Los tonos de tu wallet le llegan por el micrófono del auricular, y la
transacción firmada vuelve al auricular: los tonos hablan
[ur-tones](https://github.com/ndssigner/ur-tones/blob/main/SPEC.md), que puedes oír y descifrar con su
[herramienta web](https://ndssigner.github.io/ur-tones/).

## Hace

- Recibe tu semilla por el auricular (con un PIN de 12 caracteres o más) o
  tecleada en el teclado, y nunca la dice: solo su huella.
- Te lee cada salida de una transacción, el cambio y la comisión; la
  dirección entera si la pides.
- Habla inglés; los demás idiomas son paquetes aparte (aquí, español).

## No hace

- Guardar nada: al apagarlo, la semilla desaparece.
- Frase de contraseña, Taproot ni multifirma.
- Evitar que un teléfono recuerde las últimas teclas marcadas: el firmador
  lo avisa.
