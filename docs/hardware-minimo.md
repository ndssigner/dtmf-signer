# DTMF-Signer mínimo — Especificación de hardware (borrador)

**Estado:** propuesta de diseño, sin probar en hardware.
**Plataforma:** Raspberry Pi Pico 2 (RP2350), bare-metal, sin sistema operativo.

---

## 1. Filosofía

- **Lo mínimo posible.** El firmador solo firma. La pantalla y el teclado los pone el usuario, y pueden ser aparatos que ya tenga por casa: una tele, un teclado PS/2, un terminal serie, una calculadora o un ordenador vintage.
- **Montar y listo.** El proyecto ofrece una PCB y un diseño de protoboard. El usuario solo tiene que flashear la Pico 2 y pincharla en su zócalo.
- **Sin cajas negras.** Todo el camino de la señal es código propio y auditable. No hay módulos con firmware cerrado (lectores QR, cámaras); para firmar con QR ya existen otros proyectos.
- **Comunicación airgapped por audio** con [ur-tones](https://github.com/ndssigner/ur-tones), por el aire o por cable.
- **Para entusiastas.** Los puertos de la placa vienen protegidos. Para todo lo demás, documentamos bien las interfaces y la comunidad las adapta a sus aparatos por los GPIO libres.

## 2. Puertos de la placa

| Puerto | Conector | Para qué |
|---|---|---|
| Audio ENTRADA | Jack TRS 3,5 mm con contacto de detección | Recibir ur-tones por cable |
| Audio SALIDA | Jack TRS 3,5 mm con contacto de detección | Emitir ur-tones por cable |
| Micrófono + altavoz | Integrados | ur-tones por el aire |
| Serie RS-232 | DE-9 macho (DTE), con MAX3232 | Terminales y ordenadores con puerto serie |
| Serie TTL | Tira de 4 pines, 3,3 V | Aparatos con UART TTL |
| Teclado | Mini-DIN 6 hembra (PS/2), con adaptador de niveles | Teclear la seed, navegar |
| Vídeo | RCA compuesto, blanco y negro | Tele o monitor como pantalla pasiva |
| Alimentación | USB-C **solo alimentación** (sin líneas de datos) | Power bank o cargador de 5 V |
| Ampliación | Tira con GPIO libres + 3V3 + 5V + GND | Lo que la comunidad quiera |
| Controles | Botones CONFIRMAR y CANCELAR, 3 LEDs | Confirmación física y estado |

Por qué dos jacks TRS en vez de uno TRRS:
- Evita el lío de los dos estándares de conector de los móviles (CTIA y OMTP).
- Los ordenadores vintage (datasette del C64, EAR/MIC del Spectrum, MSX, Amstrad CPC) usan entrada y salida separadas.
- Para conectar un móvil basta un cable divisor de auriculares (~2 $).

## 3. Lista de materiales aproximada

| Componente | Coste aprox. |
|---|---|
| Raspberry Pi Pico 2 | 5 $ |
| 2 tiras hembra de 20 pines (zócalo de la Pico) | 0,5 $ |
| 2 jacks TRS 3,5 mm con contacto de detección | 0,6 $ |
| Micrófono electret + MCP6001 + pasivos | 1 $ |
| Altavoz de 8 Ω + PAM8302, o piezo | 0,5-3 $ |
| MAX3232 + 4 condensadores + DE-9 macho | 1,5 $ |
| Mini-DIN 6 hembra + 2 BSS138 + resistencias | 1 $ |
| RCA hembra + 2 resistencias | 0,3 $ |
| Conector USB-C solo alimentación + 2 × 5,1 kΩ + Schottky | 0,7 $ |
| 2 pulsadores, 3 LEDs, resistencias, tiras de pines | 0,6 $ |
| PCB (tirada pequeña) | ~2 $ |
| **Total** | **~15-20 $** |

## 4. Alimentación

- **Entrada por USB-C solo alimentación**, desde un power bank o cualquier cargador de 5 V. El conector necesita **dos resistencias de 5,1 kΩ en CC1 y CC2**; sin ellas, los cargadores USB-C a USB-C no dan tensión. La regulación la hace la fuente.
- **Las líneas de datos del USB-C no se conectan a nada.** Aunque alguien lo enchufe a un ordenador, no hay camino de datos: es un airgap por hardware.
- Del raíl de 5 V salen:
  - La alimentación del **teclado PS/2** (la norma PS/2 admite consumos de hasta 275 mA).
  - **VSYS de la Pico, a través de una Schottky**, para que pueda convivir con el micro-USB de la propia Pico (que lleva su propio diodo).
- **El micro-USB de la Pico solo se usa para flashear** (modo desarrollo). No se conecta en uso normal.
- **Ojo con el apagado automático de los power banks:** muchos se apagan si el consumo baja de unos 50-100 mA. La Pico 2 sola consume menos. Hay que documentar qué power banks tienen modo de baja corriente, o añadir una carga de mantenimiento con pulsos periódicos. *Pendiente de medir.*

## 5. Asignación de pines

| GPIO | Uso | Notas |
|---|---|---|
| GP0 / GP1 | UART0 TX / RX → MAX3232 → DE-9 | RS-232 |
| GP2 | Botón CONFIRMAR | Activo a nivel bajo, pull-up |
| GP3 | Botón CANCELAR | Activo a nivel bajo, pull-up; mantenido al arrancar = modo desarrollo |
| GP4 / GP5 | UART1 TX / RX → tira TTL | 3,3 V |
| GP6 / GP7 / GP8 | LEDs de estado / actividad / error | |
| GP9 | Detección de cable en jack ENTRADA | |
| GP10 / GP11 | PS/2 datos / reloj, a través de BSS138 | PIO, pines consecutivos |
| GP12-GP19 | **Libres en la tira de ampliación** | Coinciden con el bloque HSTX del RP2350: permiten añadir DVI, VGA, I2C, etc. |
| GP20 | Salida de audio PWM | Filtro RC y salida a jack/altavoz |
| GP21 / GP22 | Compuesto: sincronía / vídeo | PIO, pines consecutivos |
| GP25 | LED de la placa | Latido |
| GP26 (ADC0) | Audio del micrófono | **Máx. 3,3 V** |
| GP27 (ADC1) | Audio de línea (jack ENTRADA) | **Máx. 3,3 V** |
| GP28 (ADC2) | Libre en la tira de ampliación | Entrada analógica |
| GP29 (ADC3, interno) | VSYS/3 | Ya cableado en la placa |

El contacto de detección del jack de SALIDA desconecta el altavoz de forma pasiva al enchufar un cable. No gasta ningún GPIO.

## 6. Etapas de audio (orientativas)

**Entrada por micrófono:**
- Electret polarizado con 2,2 kΩ a 3V3.
- Condensador de acople de 1 µF.
- Amplificador no inversor con ganancia de 20 a 50, centrado a 1,65 V con un divisor de 2×10 kΩ.
- Salida a GP26.

**Entrada de línea:**
- Condensador de acople de 1 µF.
- Centrado a 1,65 V con 2×10 kΩ.
- Resistencia de 1 kΩ en serie como protección.
- Salida a GP27.

**Salida:**
- PWM en GP20 con portadora alta (cientos de kHz).
- Filtro RC de 2 etapas con corte en torno a 3-4 kHz (por ejemplo 1 kΩ y 47 nF).
- Condensador de acople de 10 µF hacia el jack de SALIDA.
- Un puente para elegir nivel de línea o de micrófono (divisor ~100:1) cuando el destino es la entrada de micro de un móvil.
- Desde el mismo punto, a través del contacto de conmutación del jack, al PAM8302 y al altavoz.

El RP2350 no tiene DAC. Por eso la salida es PWM filtrado, más que suficiente para DTMF.

## 7. Vídeo compuesto

- **Blanco y negro**, sin portadora de color. La misma señal sirve para teles PAL y NTSC. El firmware elige 50 o 60 Hz.
- **Dos resistencias** desde GP21 (sincronía) y GP22 (vídeo) hasta el RCA, sobre la carga de 75 Ω de la tele. Valores orientativos: ~680 Ω para la sincronía y ~220-330 Ω para el vídeo, que dan ~0 V de sincronía, ~0,3 V de negro y ~1 V de blanco. *A ajustar en el prototipo.*
- **Modo texto de 40 columnas**, legible en una tele de tubo. Una dirección bech32 ocupa 2 líneas.
- **Pantallas típicas:** teles de tubo, teles planas con entrada AV, monitores de cámara de marcha atrás y pantallas pequeñas de 5 V.
- VGA y DVI quedan como ampliaciones de la comunidad por GP12-GP19.

## 8. Niveles eléctricos

Los puertos de la placa vienen protegidos: el MAX3232 para RS-232 y los BSS138 para PS/2. Para lo que se conecte por la tira de ampliación, cada entusiasta adapta su aparato. Lo que hay que saber:

- **Pines digitales del RP2350:** según fuentes secundarias, toleran hasta 5,5 V **solo con el chip alimentado**. Durante el arranque pueden dañarse. Se recomienda adaptador de niveles o resistencias en serie. *Pendiente de confirmar en el datasheet del RP2350.*
- **Pines del ADC (GP26-GP28): nunca más de 3,3 V.** No toleran 5 V.
- **RS-232 (±12 V)** por la tira de ampliación: siempre con un MAX3232 o equivalente.

## 9. Modelo de confianza

| Tipo de periférico | Ejemplos | Dentro de la frontera de confianza | Puede ver la seed |
|---|---|---|---|
| **Pasivo** (lo controla el firmador) | Tele por compuesto, LEDs | Sí | Solo enmascarada o palabra a palabra |
| **Entrada directa** | Teclado PS/2, botones | Sí | Sí, es por donde entra |
| **Inteligente** (tiene su propio procesador y software) | Terminal por RS-232 o TTL: C64, HP-48, Palm, PC | **No** | **Nunca** |
| **Canal de audio** | Móvil, ordenador, magnetófono | No | Solo la seed cifrada con PIN de ur-tones |

Reglas:
1. **La seed nunca entra ni sale por los puertos serie.** Solo por DTMF (recomendado con cable; por el aire, siempre con PIN) o por el teclado PS/2.
2. **Firmar requiere pulsar CONFIRMAR en el propio firmador.** Ningún terminal puede firmar por su cuenta.
3. **Sin estado:** la seed vive solo en RAM y se pierde al apagar. Nada se escribe en flash.
4. **USB de la Pico desactivado al arrancar.** Solo se activa manteniendo CANCELAR pulsado al encender (modo desarrollo: actualizar y releer el firmware para verificarlo). El USB-C de alimentación no tiene datos.
5. **Nonces deterministas** (RFC 6979 para ECDSA; BIP340 sin aleatoriedad auxiliar obligatoria). No depende de ningún generador de aleatorios.
6. Cuando la transacción se revisa en un terminal serie, ese terminal es el que la verifica. Debe documentarse así para el usuario.
7. **No mostrar la seed en teles conectadas a internet.** Las smart TV pueden analizar y enviar lo que muestran (ACR), por cualquier entrada. Para la seed, mejor una tele vieja.
8. **Las señales de vídeo analógicas pueden captarse a distancia** con equipo especializado (*van Eck phreaking*). La seed nunca se muestra entera de una vez.

## 10. Protocolo serie (esbozo)

- 8N1, **9600 baudios por defecto**, configurable hasta 115200. El mismo protocolo en RS-232 y en TTL.
- **Texto ASCII en líneas terminadas en CRLF**, para que **cualquier programa de terminal existente** funcione sin software específico.
- PSBT de entrada y salida en texto UR (`ur:crypto-psbt/...`), el mismo formato que ur-tones, o en base64.
- Ejemplo de diálogo:

```
> PSBT recibida: 2 salidas
  1) bc1q...xyz   0.01500000
  2) cambio       0.00480000
  comisión: 1200 sat
Pulse CONFIRMAR en el firmador para firmar
```

La especificación completa del protocolo es un documento aparte, pendiente.

## 11. Formatos de montaje

- **Protoboard:** la Pico 2 pinchada directamente, más placas adaptadoras comerciales (módulo MAX3232 con DE-9, adaptador PS/2, USB-C de alimentación, RCA). Para prototipar sin soldar. *Esquema pendiente.*
- **PCB:** todo de agujero pasante, con la Pico en zócalo de tiras hembra. Diseño en KiCad, publicado como hardware abierto. *Pendiente.*

## 12. Módulos de firmware

| Módulo | Contenido |
|---|---|
| `audio` | Goertzel sobre el ADC y síntesis DTMF por PWM |
| `urtones` | Tramas, Reed-Solomon, fountain codes (reutilizar `c/ur_tones.c`) |
| `crypto` | libsecp256k1, SHA-256/512, BIP39, BIP32 |
| `psbt` | Parser BIP174, sighash segwit v0 (BIP143) y Taproot (BIP341) |
| `video` | Compuesto B/N por PIO, modo texto de 40 columnas |
| `serial` | Terminal de texto por RS-232 y TTL |
| `input` | Botones y teclado PS/2 por PIO |
| `system` | Arranque, desactivar USB, modo desarrollo, VSYS |

## 13. Pendiente de verificar

- Tolerancia a 5 V del RP2350 en su datasheet.
- Ruido del ADC de la Pico 2 con la etapa de micrófono propuesta.
- Tiempos de tono más cortos que los de ur-tones v0 con Goertzel.
- Valores de las resistencias del compuesto y estabilidad de la imagen en teles PAL y NTSC.
- Consumo total y comportamiento con power banks que se apagan con poca carga.
- Rendimiento de libsecp256k1 y PBKDF2 (BIP39) en el RP2350.
- Errata del RP2350 sobre pull-downs internos: usar pull-ups para los botones.

## Referencias

- [ur-tones](https://github.com/ndssigner/ur-tones) y su [especificación](https://github.com/ndssigner/ur-tones/blob/main/SPEC.md)
- [pico-signer](https://github.com/notemilo/pico-signer): firmador sobre Pico 2 con lector QR por UART (MIT)
- [Raspberry Pi Pico 2](https://www.raspberrypi.com/products/raspberry-pi-pico-2/)
