[English](README.md) · Español

*Traducción fiel del [README en inglés](README.md). Si difieren, manda la
versión inglesa.*

# DTMF-Signer

Un firmador de Bitcoin mínimo, barato y de hardware abierto que cualquiera
puede montar: una Raspberry Pi Pico 2, una pantalla pequeña, cinco botones,
un micrófono y un piezo. Se comunica con tu cartera mediante tonos de
teléfono (DTMF) a través de [ur-tones](https://github.com/ndssigner/ur-tones),
al aire o por cable. Sin cámara, sin datos por USB, sin elemento seguro y sin
almacenamiento: la semilla vive en memoria solo mientras firmas. Del proyecto
[NDS-Signer](https://github.com/ndssigner/nds-signer).

**La idea central:** la semilla llega como tonos, creados con ur-tones en un
ordenador desconectado, y **nunca se muestra, por ningún canal**: solo su
huella. A partir de ahí, tú eliges: seguir todo por audio, para el máximo
aislamiento, o manejarlo desde un terminal vintage por un puerto serie.
DTMF-Signer y ur-tones están pensados para ir juntos.

> **Estado: fase de diseño.** Todavía no hay firmware ni hardware. Este
> repositorio contiene la especificación de hardware y el análisis de
> riesgos, de momento en español. No lo uses con fondos.

## Motivación

- **Un firmador para todos.** Funciona solo, con cualquier móvil u
  ordenador que pueda reproducir y oír sonido. Sin cámara, sin cables
  especiales, sin tiendas de aplicaciones.
- **Bajo coste.** Unos 13 $ en piezas para la placa básica (una
  estimación), más o menos un tercio de un firmador que lee códigos QR con
  un módulo lector.
- **Hazlo tú mismo, fácil y a prueba de torpes.** Pincha una Pico 2 en una
  placa ya montada, sin soldar, o conecta módulos comunes en una
  protoboard. Piezas genéricas que se compran en cualquier parte: los tonos
  se decodifican por software, así que no hacen falta chips decodificadores
  de DTMF, difíciles de encontrar.
- **Frente a los ataques en la cadena de suministro y la entropía falsa.**
  - En octubre de 2026 se descubrió que aparatos Ledger vendidos por
    distribuidores llevaban un implante oculto que leía la frase de
    recuperación de la pantalla durante la configuración y la enviaba por
    la red móvil; las pérdidas se estiman en decenas de millones de dólares
    ([CoinDesk](https://www.coindesk.com/tech/2026/10/13/ledger-says-spy-chip-inside-cryptobilis-ledger-devices-transmitted-wallet-keys-over-mobile-network)).
    Aquí el firmador lo montas tú con piezas genéricas, el microcontrolador
    lo has comprado y flasheado tú y puedes sacarlo de su zócalo, y cada
    pieza de la placa está abierta y a la vista. Además, la semilla nunca
    se muestra en su pantalla: un implante que la leyera no encontraría
    nada que robar.
  - También en 2026, un error de compilación en el firmware de Coldcard le
    hizo generar semillas con un sustituto por software débil en lugar de
    su generador de números aleatorios por hardware, y las carteras creadas
    así fueron vaciadas
    ([crypto.news](https://crypto.news/coldcard-firmware-bug-drains-38-million-bitcoin/)).
    DTMF-Signer nunca genera semillas, y sus firmas usan nonces
    deterministas: no tiene ningún generador de números aleatorios que
    pueda fallar.

Además, **no tiene cajas negras** (ni cámara ni módulo QR con firmware
cerrado: todo el camino de la señal, del micrófono al decodificador, es
código propio) y **se lleva bien con las máquinas antiguas**: un ordenador
o una calculadora vintage le pueden prestar pantalla y teclado por un
puerto serie.

## Cómo funciona

```
  cartera + herramienta ur-tones        DTMF-Signer
 ┌──────────────────────┐   tonos   ┌───────────────────────┐
 │ PSBT sin firmar ─────┼─────────► │ micrófono → decodif.  │
 │                      │           │ pantalla + 5 botones  │
 │ PSBT firmada ◄───────┼───────────┤ piezo ← codificador   │
 └──────────────────────┘   tonos   └───────────────────────┘
```

1. La semilla llega como tonos, cifrada con un PIN. El PIN se teclea en el
   firmador y nunca suena. La passphrase, si usas una, llega por cable o se
   teclea en el firmador.
2. El firmador muestra la huella de la semilla, para comprobar que es la
   correcta. La semilla en sí nunca se muestra.
3. La PSBT sin firmar llega como tonos.
4. El firmador muestra en su pantalla cada salida, cada importe y la
   comisión. Las recorres todas y mantienes pulsado un botón para firmar.
5. La PSBT firmada vuelve como tonos.

Lo desenchufas y la semilla desaparece. Del paso 3 en adelante, las PSBT
pueden viajar como tonos o a través de un terminal vintage por el puerto
serie: tú eliges.

## Uso cotidiano

1. Genera la semilla fuera del firmador: con dados, o con otra cartera de
   tu confianza.
2. En un **ordenador desconectado**, la herramienta web de ur-tones la
   convierte en tonos, protegida con un **PIN de 12 caracteres o más**
   (puede generarlo por ti), y los guarda como archivo **WAV**.
3. Apunta el PIN en papel o guárdalo en un gestor de contraseñas, **siempre
   separado del WAV**.
4. Guarda el WAV en un **reproductor de audio sin conexión a internet**,
   como un reproductor MP3.
5. Para firmar: conecta el reproductor **por cable** al jack de entrada del
   firmador y reproduce el WAV. Teclea el PIN en el firmador y comprueba la
   huella.
6. Después, firma: las PSBT van y vienen por el aire con tu cartera, o a
   través de un terminal vintage.

El WAV es una copia cifrada de tu semilla: quien lo consiga puede probar
PINs sin conexión, así que usa un PIN largo. Y **no debe ser tu única copia
de seguridad**: los reproductores se estropean. Guarda también la semilla
por otra vía (papel, metal, [SeedCraft](https://github.com/ndssigner/seedcraft)…).

## La placa básica

| Pieza | Para qué |
|---|---|
| Raspberry Pi Pico 2 en zócalo | El microcontrolador. La compras y la flasheas tú |
| OLED de 1,3", 128 × 64 | Revisar lo que firmas, introducir PIN y passphrase |
| Cruceta de 5 botones | Moverse, teclear, confirmar |
| Micrófono y piezo | Tonos por el aire |
| Jack de audio solo de entrada | Un canal por cable para los datos sensibles |
| USB-C solo de alimentación | Sin ninguna línea de datos |

Los puertos opcionales vienen como **huellas sin montar**, para quien los
quiera: serie RS-232 o TTL, teclado PS/2, vídeo compuesto en blanco y negro,
salida de línea, pilas y una tira con los GPIO libres.

## Seguridad en resumen

- **La semilla nunca se muestra.** Ni en la pantalla, ni por la salida de
  vídeo, ni por el puerto serie, ni por audio. Solo su huella.
- **Sin estado.** La semilla vive solo en memoria. Nada se escribe en la
  flash. Desenchufarlo, apagarlo o dejarlo inactivo la borra.
- **El PIN y la passphrase nunca van por el aire.** Los datos sensibles
  llegan por cable o se teclean en el firmador, y nunca salen de él, por
  ningún canal.
- **Lo revisas en el firmador.** Cada salida se muestra en su propia
  pantalla, y nada se firma sin su botón físico.
- **Ningún generador de números aleatorios que pueda fallar.** Las firmas
  usan nonces deterministas (RFC 6979, BIP 340).
- **Sin elemento seguro, a propósito.** Apagado, no hay nada que extraer.
- **Comprar te vincula a Bitcoin.** Cualquier pedido de una placa montada
  deja tu nombre y tu dirección en la base de datos de alguien. Las piezas
  genéricas compradas por separado para la versión en protoboard son el
  camino más privado.

El análisis completo, por formato de distribución y por uso, está en
[`docs/analisis-riesgos.md`](docs/analisis-riesgos.md).

## Formas de montarlo (previstas)

- **Protoboard + lista de materiales**, con módulos comerciales comunes.
- **Placa perforada:** los mismos módulos soldados en una placa de
  prototipos con el trazado de una protoboard, con la Pico y la pantalla en
  tiras hembra. El montaje de la protoboard se pasa tal cual. Más sólida y
  portátil, sin pedir ninguna PCB.
- **PCB "shell":** montada en fábrica, sin la Pico ni la pantalla. Las
  compras tú, flasheas la Pico y las pinchas.
- **La misma PCB con la Pico y la pantalla en sus zócalos**, la Pico sin
  flashear, por comodidad. Reflashéala antes de usarla.
- **Una PCB de agujero pasante** para pedirla vacía y soldarla a mano.

## Hoja de ruta

- [ ] Decodificador de tonos por software en una Pico 2, probado con ur-tones
- [ ] Esquema, guías de protoboard y placa perforada, y lista de materiales
- [ ] Dos diseños de PCB: SMD (montada en fábrica) y de agujero pasante
- [ ] Simulación de las etapas analógicas y del firmware
- [ ] Firmware: audio, ur-tones, PSBT, firma, pantallas
- [ ] Protocolo serie para terminales vintage
- [ ] Revisión independiente

## Aviso

DTMF-Signer es un diseño experimental y se ofrece tal cual, sin ninguna
garantía. Todavía no existe ningún aparato funcionando, y nadie
independiente lo ha revisado. No respondemos de pérdidas de fondos ni de
ningún otro daño, ni del uso que cada cual haga de este diseño.

## Qué hay en el repositorio

- [`docs/hardware-minimo.md`](docs/hardware-minimo.md): la especificación de
  hardware: placa, pines, audio, pantallas, política de canales, formatos de
  distribución.
- [`docs/analisis-riesgos.md`](docs/analisis-riesgos.md): el análisis de
  riesgos: cadena de suministro, privacidad del comprador, firmware, uso y
  amenazas físicas.

## Proyectos hermanos

- [NDS-Signer](https://github.com/ndssigner/nds-signer): un firmador sin
  conexión para la Nintendo DSi, que firma PSBT por QR o por tonos.
- [ur-tones](https://github.com/ndssigner/ur-tones): PSBT y semillas como
  tonos de teléfono, por cable o al aire.
- [SeedCraft](https://github.com/ndssigner/seedcraft): una semilla guardada
  como el orden de unos objetos, que se vuelve a leer a mano o con una
  herramienta web.

## Apoyar el proyecto

DTMF-Signer es gratuito y no tiene financiación. Las donaciones ayudan a
mantenerlo:

<table>
<tr><td align="center"><img src="docs/images/donate-bitcoin.png" width="180" alt="Bitcoin"><br><b>Bitcoin</b><br><code>bc1qx5snc0wlc8cg9gwxhyx27y6pkru8rnngyq7uja</code></td>
<td align="center"><img src="docs/images/donate-lightning.png" width="180" alt="Lightning"><br><b>Lightning</b><br><code>ndssigner@coinos.io</code><br><sub>LNURL (para carteras sin direcciones Lightning):<br><code>LNURL1DP68GURN8GHJ7CM0D9HX7UEWD9HJ7TNHV4KXCTTTDEHHWM30D3H82UNVWQHKUERNWD5KWMN9WGQ8XE42</code></sub></td></tr>
</table>

Las mismas direcciones que [NDS-Signer](https://github.com/ndssigner/nds-signer#donate).

## Licencia

Sin decidir todavía. El hardware se publicará previsiblemente bajo
[CERN-OHL-S v2](https://ohwr.org/cern_ohl_s_v2.txt).
