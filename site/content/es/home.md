# Firma Bitcoin con tonos de teléfono, en un aparato que construyes tú

DTMF-Signer es una **especificación**: dice qué debe hacer un firmador de
Bitcoin y deja libre cómo construirlo. Habla con tonos de teléfono (DTMF),
así que no necesita cámara ni una pantalla sofisticada. Basta un
microcontrolador barato y unas pocas piezas de una tienda de electrónica, o
rescatadas de la chatarra.

## Por qué

- **Cualquiera, en cualquier lugar.** Las piezas son baratas y fáciles de
  encontrar en todo el mundo, nuevas o rescatadas de teléfonos y aparatos
  viejos.
- **Más personas.** Cada montaje sirve a gente distinta: uno de ellos, en
  preparación, se usa como una centralita telefónica, con locuciones y el
  teclado, y podría servir a quien no puede ver una pantalla.
- **Nadie entre tú y tu firmador.** Un firmador comercial se puede manipular
  o interceptar de camino a tu casa. Uno que construyes tú, con piezas
  genéricas compradas en cualquier sitio, no le da a un atacante ningún
  envío al que apuntar.

## Cómo empezó

La culpa es de una canción pop. En ["Catch Catch"](https://www.youtube.com/watch?v=NOiyDlWl534),
la cantante de K-pop Yena usa los pitidos del teclado de un teléfono como
parte de la música. En algún momento entre el segundo estribillo y la
tercera escucha seguida surgió una pregunta muy poco musical: si una
canción puede llevar DTMF, ¿por qué no una transacción de Bitcoin? Súmale
el recién terminado [NDS-Signer](https://github.com/ndssigner/nds-signer),
que acababa de enseñar a firmar a una Nintendo DSi, y DTMF-Signer era el
paso obvio. Así que sí: este firmador existe porque alguien no podía dejar
de escuchar una canción. Sin arrepentimientos.

## Los montajes

Cada montaje es un perfil de la especificación, con su lista de piezas, su
plano y, pronto, un simulador que ejecuta el firmware real en tu navegador.
