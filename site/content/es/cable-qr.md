El montaje de referencia: **escucha por cable y habla por QR**. Los tonos le
llegan por un cable de auriculares desde el móvil o el ordenador que tiene
tu wallet; la transacción firmada vuelve como códigos QR en su propia
pantalla pequeña, que tu wallet lee con la cámara. No tiene micrófono ni
altavoz, así que no oye la habitación ni se le oye en ella.

Es el montaje que funciona hoy, en una protoboard o una placa perforada, con
una Raspberry Pi Pico, una pantalla OLED pequeña, unos pocos pulsadores y un
puñado de resistencias y condensadores.

## Qué hace

- Recibe tu semilla como tonos, protegida con un PIN, y nunca la muestra:
  solo su huella.
- Muestra la cuenta de tu wallet como QR, para que tu wallet la vigile.
- Recibe una transacción para firmar, te enseña en su pantalla qué gasta y
  adónde lo envía, y solo firma cuando confirmas con sus botones.

## Qué no hace

- No guarda nada: al apagarlo, la semilla desaparece.
- Aún no hace Taproot ni multifirma.
