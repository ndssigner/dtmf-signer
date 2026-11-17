Un montaje **sin pantalla ni botones: un teléfono de tonos es toda la
interfaz**. El firmador alimenta la línea del teléfono, como una centralita.
Tecleas en el teclado; te habla por el auricular, te lee qué gasta una
transacción y adónde lo envía, y solo firma cuando cuelgas un momento y
vuelves a descolgar. Los tonos de tu wallet le llegan por el micrófono del
auricular, y la transacción firmada vuelve al auricular como tonos.

Está diseñado y simulado, aún no montado: una Raspberry Pi Pico, un
elevador de tensión pequeño y una fuente de corriente para la línea, una
roseta RJ11 y cualquier teléfono de tonos con auricular.

Puedes probarlo abajo, en el simulador: el propio firmware, compilado a
WebAssembly, con un teléfono y una wallet de prueba junto a su auricular.

## Qué hace

- Recibe tu semilla por el auricular (con un PIN de 12 caracteres o más,
  tecleado en el teclado) o tecleada en el teclado, y nunca la dice: solo su
  huella.
- Te lee cada salida de una transacción, el cambio y la comisión; la
  dirección entera si la pides.
- Habla inglés; los demás idiomas son paquetes aparte (aquí, español).

## Qué no hace

- No guarda nada: al apagarlo, la semilla desaparece.
- Sin frase de contraseña, Taproot ni multifirma.
- Un teléfono puede recordar las últimas teclas marcadas: el firmador lo
  avisa.
