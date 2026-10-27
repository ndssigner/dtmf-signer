# Disposición en protoboard

*[Read in English](README.md)*

El montaje de referencia del perfil **Cable y QR** en una protoboard de
tamaño completo (830 puntos): una Raspberry Pi Pico, la OLED SH1106 de 1,3"
por SPI, cinco botones en un solo pin del ADC y una bornera de tres
contactos para el cable de audio. La misma disposición vale para una
protoboard soldable (una placa perforada con las tiras y los raíles de una
protoboard), y su plantilla impresa saldrá de este fichero.

**Estado:** diseñada y comprobada por sus conexiones; todavía no se ha
montado a partir de este fichero. Los circuitos en sí (pantalla, botones,
entrada de línea) se probaron en una protoboard anterior.

## Ficheros

- [`dtmf-signer-breadboard.diy`](dtmf-signer-breadboard.diy): la
  disposición, para
  [DIY Layout Creator](https://github.com/bancika/diy-layout-creator) 6.8 o
  posterior (gratuito).
- [`template-en.pdf`](template-en.pdf) y [`template-es.pdf`](template-es.pdf):
  la plantilla imprimible, a escala 1:1 en A4 apaisado.
- [`tools/check_diy.py`](../../tools/check_diy.py) la lee y muestra qué
  conecta cada pieza; `tests/test_check_diy.py` compara esas conexiones con
  lo que espera el firmware.

## Plantilla imprimible

Imprímela al 100 %, sin escalar, y comprueba que la barra del pie de la
página mide 100 mm. Recorta la placa por su contorno (todo lo que necesita
está dentro) y pégala sobre la protoboard soldable, agujero sobre agujero,
antes de poner las piezas; perfora los agujeros que uses. Marca:

- cada pieza en su sitio, con su valor; el + y el − de los condensadores
  electrolíticos, con una banda en el lado −, como en la pieza; el pin 1 y
  el extremo del USB de la Pico; los pines de la pantalla y el contorno del
  módulo, que sobresale de la placa;
- los botones (flecha y nombre, justo debajo del pulsador, entre las filas
  i y j) y los contactos de la bornera de audio (icono y nombre, entre el
  borde inferior de la bornera y el raíl de debajo), para que se lean con
  todo montado; qué es cada raíl, a su lado;
- los agujeros que se usan, bien visibles; los demás, apenas marcados, solo
  para encajar la hoja;
- en el extremo libre de la placa: el proyecto, los controles principales y
  un código QR de la [herramienta web de ur-tones](https://ndssigner.github.io/ur-tones/),
  que envía los tonos al firmador y lee lo que devuelve.

Para generarla de nuevo tras cambiar la disposición:

```
pip install -r tools/requirements-template.txt
python3 tools/breadboard_template.py hardware/breadboard/dtmf-signer-breadboard.diy --lang es --out hardware/breadboard/template-es.pdf
```

El PDF no lleva fecha, así que la misma disposición da siempre el mismo
fichero.

## Cómo está colocada

Sujeta la placa en horizontal, como en la plantilla: la Pico a la izquierda y la pantalla arriba:

- **Pantalla:** sus pines junto al canal central; su cuerpo tapa esa mitad
  de la placa y sobresale por el borde. Por debajo pasan cables (quedan 2–3
  mm entre la placa y la pantalla).
- **Botones**, al lado de la pantalla, de izquierda a derecha: IZQUIERDA,
  ARRIBA, OK, ABAJO, DERECHA, a 10 mm. Cada uno va por su resistencia (1 k,
  2,2 k, 4,7 k, 10 k, 22 k) a un raíl, que es el nodo de la escalera y va a
  GP28 con un pull-up de 10 k. **Ese raíl no es 3V3**: no alimentes nada desde
  él.
- **Bornera de audio:** 1 OUT, 2 COM (masa), 3 IN.
  - La entrada (IN) pasa por 1 µF (su − hacia la bornera), se centra en 1,65
    V con dos resistencias de 10 k y llega a GP27 por 1 k y 4,7 k, con 10 nF a
    masa.
  - La salida (OUT) sale de GP15 por 1 k y 10 µF (su − hacia la bornera). El
    firmware todavía no la usa.
- **Pico:** en el extremo izquierdo, con el USB en el borde de la placa.
- **Raíles:** la disposición da cada raíl por continuo de punta a punta.
  Compruébalo en la tuya con el multímetro; si un raíl se corta a la mitad,
  puentéalo.

## El cable de audio

Corta el cable de unos auriculares baratos con micrófono (clavija de 3,5 mm
de cuatro contactos), o usa un jack o una clavija de 3,5 mm ya cableados, y
atornilla sus hilos a la bornera:

| Bornera | Hilo (clavija CTIA, la habitual) |
|---|---|
| 3 IN | punta: canal izquierdo de la salida de auriculares del móvil u ordenador |
| 2 COM | anillo 2: masa |
| 1 OUT | manguito: micrófono |

Los colores de los hilos cambian de unos auriculares a otros: identifica
cada uno con el multímetro en continuidad, desde los contactos de la
clavija hasta los hilos cortados. Las clavijas antiguas OMTP intercambian
masa y micrófono. OUT está pensada para una entrada de micrófono pero no se
ha probado, y una entrada de micrófono puede necesitar bajar el nivel con un
divisor de resistencias.

## En placa perforada

- Pon tiras de pines hembra bajo la Pico y la pantalla, a modo de zócalo:
  se pueden quitar y los cables siguen pasando por debajo.
- Usa cable rígido aislado. Los cables pueden cruzarse; lo que importa es
  que cada pata vaya a su propio agujero.
