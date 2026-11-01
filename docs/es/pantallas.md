# Las pantallas

Cómo se ve la versión de la Pico y cómo se navega por ella, en su OLED de
128 × 64 con cinco botones. Las imágenes no son fotos de la placa: salen del
propio código del firmware ejecutado en un ordenador, píxel a píxel, solo con
datos de prueba (la semilla 00…00, PIN `TEST`, passphrase `Ab 1!`). Para
generarlas de nuevo tras un cambio:

```
cmake --build build --target screens
python3 tools/render_screens.py build/tests/c/screens docs/images/screens
```

## Los botones

| Botón | Qué hace |
| :--- | :--- |
| ▲ ▼ | Moverse por menús y páginas |
| ◀ ▶ | Moverse en el selector; ◀ vuelve atrás en casi todas las pantallas |
| OK | Elegir, continuar, añadir un carácter |
| OK mantenido (1,5 s) | Terminar el PIN o la passphrase; firmar, en la última página de la revisión |
| ◀ mantenido (3 s) | **Borrado de emergencia**, en cualquier pantalla |

La pantalla va girada 180 grados por defecto, como la monta la protoboard.

## Cargar la semilla

### El menú de inicio

![Menú de inicio](../images/screens/home.png)

▲ ▼ mueven la selección y OK elige. `Load seed` empieza a escuchar. Los tres
ajustes cambian con OK: la red (mainnet o testnet), el contraste de los QR y
su tamaño (2 px por módulo, o 1 px para códigos más grandes).

### Escuchando

![Escuchando, en espera](../images/screens/seed-waiting.png)
![Escuchando, recibiendo](../images/screens/seed-receiving.png)

Reproduce los tonos de la semilla por el cable de audio. La segunda fila dice
lo que llega: `waiting` (nada), `* receiving` (se están oyendo teclas), y las
teclas se van contando. Las teclas de una semilla se cuentan, nunca se
muestran. `lost` cuenta el audio que la placa no pudo seguir. ◀ vuelve al
inicio.

![Demasiado flojo para leer](../images/screens/too-quiet.png)
![No es una semilla](../images/screens/not-a-seed.png)

`! too quiet to read` quiere decir que llega sonido pero no se puede leer
ninguna tecla: sube el volumen. `Not a seed` quiere decir que ha llegado un
cuadro de otra cosa (una PSBT, por ejemplo); la escucha sigue. Si se pierde
un cuadro con los tonos demasiado flojos, debajo se añade
`Too quiet: volume up`.

### El PIN

![PIN, vacío](../images/screens/pin-empty.png)
![PIN, escribiendo](../images/screens/pin-typing.png)

La semilla llega cifrada con un PIN. Las flechas se mueven por las letras y
los dígitos, dando la vuelta por los bordes; OK añade el carácter y el cursor
se queda donde está, así que una letra repetida es OK, OK. El recuadro de la
derecha muestra el carácter elegido tres veces más grande. La casilla de la
flecha, al final, borra el último carácter; con el PIN vacío vuelve atrás.
Mantén OK para terminar (sin escribir nada: sin PIN). El PIN se ve en claro:
solo lo ve quien tiene el firmador en la mano.

![Trabajando](../images/screens/working.png)

`Working...` sale mientras se descifra la semilla y se deriva la cartera,
unos segundos en el RP2040.

### Huellas y passphrase

![Huella de la semilla](../images/screens/seed-fingerprint.png)

La huella de la semilla: compárala con la que muestra el emisor. Un PIN
equivocado da un error o una huella distinta. OK continúa, ◀ descarta la
semilla.

![¿Passphrase?](../images/screens/passphrase-ask.png)

OK para no usar passphrase, ▶ para escribir una.

![Passphrase, letras](../images/screens/passphrase-letters.png)
![Passphrase, dígitos y símbolos](../images/screens/passphrase-digits.png)

El selector de la passphrase funciona como el del PIN, en cuatro páginas de
ASCII imprimible: `abc`, `ABC`, `123` (dígitos y los símbolos más usados) y
`<{~` (el resto). Bajando desde la última fila se pasa a la página siguiente
y subiendo desde la primera a la anterior; las pestañas de abajo dicen dónde
estás. El espacio, el `.` y el `-` están en las dos páginas de letras, y la
casilla de borrar en la misma esquina de todas. Solo ASCII, a propósito: lo
explica [la especificación de hardware](../hardware-minimo.md), en lo que
dice de la passphrase.

![Huella de la cartera](../images/screens/wallet-fingerprint.png)

La huella de la cartera final, con la passphrase. Una passphrase con una
letra mal da otra cartera válida y vacía: este número es la comprobación.

## La cartera

![Lista](../images/screens/ready.png)

La cartera está cargada. `Sign PSBT` empieza a escuchar una PSBT,
`Show XPUB` muestra la cuenta como un QR animado, `Wipe` lo borra todo (como
el borrado de emergencia).

![XPUB](../images/screens/xpub.png)

La xpub para la cartera de solo lectura (Sparrow, por ejemplo). La columna
derecha da el número de partes, cuál está en pantalla (de 1 a N, y vuelta a
empezar) y, tras la primera vuelta, `looping`. ◀ vuelve atrás. El QR usa su
propio ajuste de contraste.

## Firmar

### Recibir la PSBT

![Firmar, en espera](../images/screens/sign-waiting.png)
![Firmar, recibiendo](../images/screens/sign-receiving.png)

Como con la semilla, más las últimas teclas oídas (una PSBT es pública) y las
partes recibidas de las necesarias. Que falten partes no es problema: la
codificación fountain sirve con cualquiera de ellas.

### La revisión

La revisión es una fila de tarjetas: ▲ ▼ para moverse, ◀ para cancelar;
cada tarjeta dice arriba a la derecha dónde estás.

![Revisión, una salida enviada](../images/screens/review-send.png)
![Revisión, el cambio](../images/screens/review-change.png)

Una tarjeta por salida. Arriba dice qué es: `Send to`, o, en vídeo inverso,
`Change, yours` o `To this wallet` (solo para salidas que el firmador ha
comprobado que son de esta cartera). Luego el importe con los 8 decimales,
el doble de alto, y la dirección completa en grupos de 4, que nunca se parte
entre tarjetas: compárala grupo a grupo con la de la cartera del ordenador.
En las direcciones la `g`, la `j`, la `p`, la `q` y la `y` bajan de la
línea, para que la `g` y la `q` no se confundan con el `9`.

![Revisión, la comisión](../images/screens/review-fee.png)

La comisión, y debajo lo enviado, el cambio y el total (enviado más
comisión). Si hay algo que avisar, viene una tarjeta `Check`: una comisión
alta, o entradas que no se van a firmar.

![Revisión, la confirmación](../images/screens/review-confirm.png)

La última tarjeta: lo que sale, comisión incluida, y `Hold OK: sign`.
Firmar solo se ofrece aquí, después de ver todas las tarjetas; un toque que
llegó antes de que la revisión estuviera en pantalla no cuenta, y entonces
la barra dice `Tap OK, then hold OK`.

![Rechazada](../images/screens/rejected.png)

Una PSBT que el firmador no firma da `Rejected` y el motivo, aquí una salida
de cambio que dice ser de la ruta de esta cartera con una clave ajena
(`DS_ERR_CHANGE`). No se firma nada; OK vuelve atrás.

### La PSBT firmada

![Firmada](../images/screens/signed.png)

La PSBT firmada como QR animado, que lee la cartera del ordenador. ◀ cuando
ya se haya leído.

## El borrado de emergencia

![Borrado](../images/screens/wiped.png)

Mantén ◀ 3 segundos en cualquier pantalla: la semilla, el PIN, la passphrase
y todo lo derivado se borran, y la placa se reinicia.
