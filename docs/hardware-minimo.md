# DTMF-Signer mínimo — Especificación de hardware (borrador)

**Estado:** propuesta de diseño, sin probar en hardware.
**Plataforma:** Raspberry Pi Pico 2 (RP2350), bare-metal, sin sistema operativo.

---

## 1. Filosofía

- **Lo mínimo posible.** El firmador solo firma. La pantalla y el teclado los pone quien quiera, y pueden ser aparatos que ya tenga por casa: una tele, un monitor VGA, un teclado PS/2, una calculadora o un ordenador vintage.
- **Sin cajas negras.** Todo el camino de la señal es código propio y auditable. No hay módulos con firmware cerrado (lectores QR, cámaras).
- **Comunicación airgapped por audio** con [ur-tones](https://github.com/ndssigner/ur-tones), por el aire o por cable.
- **Para entusiastas.** No buscamos una solución universal. Documentamos bien las interfaces (niveles eléctricos, pines, protocolo) e invitamos a la comunidad a adaptarlas a sus aparatos.

## 2. Núcleo obligatorio

| Componente | Función | Coste aprox. |
|---|---|---|
| Raspberry Pi Pico 2 | MCU, ADC, PWM, UART, PIO | 5 $ |
| 2 jacks TRS de 3,5 mm con contacto de detección | ENTRADA y SALIDA de audio, como un magnetófono | 0,6 $ |
| Micrófono electret + amplificador operacional (p. ej. MCP6001) + pasivos | Recepción por el aire | 1 $ |
| Piezo, o altavoz de 8 Ω con PAM8302 | Emisión por el aire | 0,5-3 $ |
| 2 pulsadores: CONFIRMAR y CANCELAR | Confirmación física, obligatoria para firmar | 0,3 $ |
| 3 LEDs + resistencias | Estado, actividad, error | 0,2 $ |
| Portapilas 3×AA | Alimentación por VSYS | 1-2 $ |
| Tira de pines de 4 vías para UART | Puerto de terminal | 0,1 $ |
| Placa perforada o protoboard | Montaje | 1-2 $ |
| **Total** | | **~10-15 $** |

Por qué dos jacks TRS en vez de uno TRRS:
- Evita el lío de los dos estándares de conector de los móviles (CTIA y OMTP).
- Los ordenadores vintage (datasette del C64, EAR/MIC del Spectrum, MSX, Amstrad CPC) usan entrada y salida separadas.
- Para conectar un móvil basta un cable divisor de auriculares (~2 $).

## 3. Módulos opcionales (comunidad)

| Módulo | Para qué | Piezas | Coste aprox. |
|---|---|---|---|
| Teclado PS/2 | Teclear la seed, navegar y confirmar | Conector mini-DIN 6 hembra (+ adaptador de niveles si se quiere) | 1-2 $ |
| Salida VGA | Usar un monitor como pantalla pasiva | Conector DE-15 hembra + resistencias | 1-2 $ |
| Salida DVI/HDMI | Monitores modernos (bloque HSTX del RP2350) | Conector o placa adaptadora DVI | 3-10 $ |
| Pantalla LCD de caracteres I2C (16×2 / 20×4) | Pantalla integrada | Módulo LCD con adaptador PCF8574 | 3-5 $ |
| Teclado 4×4 por I2C | Teclado integrado (las 16 teclas de DTMF) | Teclado de membrana + PCF8574 | 1-2 $ |
| RS-232 (DE-9) | Terminales y ordenadores con puerto serie clásico | Módulo MAX3232 | 1-2 $ |
| Lector QR por UART | Quien prefiera velocidad sobre minimalismo | Módulo comercial (firmware cerrado) | 30-40 $ |
| Link de Game Boy y otros buses | Terminales exóticos | PIO + cable del aparato | variable |

## 4. Asignación de pines propuesta

Es una propuesta: la comunidad puede reasignar pines siempre que lo documente.

| GPIO | Uso | Notas |
|---|---|---|
| GP0 | UART0 TX | 3,3 V TTL |
| GP1 | UART0 RX | 3,3 V TTL |
| GP2 | Botón CONFIRMAR | Activo a nivel bajo, pull-up |
| GP3 | Botón CANCELAR | Activo a nivel bajo, pull-up |
| GP4 / GP5 / GP6 | LEDs de estado / actividad / error | |
| GP8 / GP9 | PS/2 datos / reloj | PIO, pines consecutivos |
| GP10 | Salida de audio PWM | Filtro RC y salida a jack/amplificador |
| GP11 | Apagado del amplificador del altavoz | Silencia el altavoz cuando hay cable en SALIDA |
| GP12 | Detección de cable en ENTRADA | Contacto de conmutación del jack |
| GP13 | Detección de cable en SALIDA | Contacto de conmutación del jack |
| GP14 / GP15 | I2C1 SDA / SCL | LCD y teclado 4×4 opcionales |
| GP16-GP20 | VGA: HSYNC, VSYNC, R, G, B | 1 bit por color; suficiente para texto |
| GP25 | LED de la placa | Latido |
| GP26 (ADC0) | Audio del micrófono | **Máx. 3,3 V** |
| GP27 (ADC1) | Audio de línea (jack ENTRADA) | **Máx. 3,3 V** |
| GP28 (ADC2) | Libre | |
| GP29 (ADC3, interno) | VSYS/3: nivel de batería | Ya cableado en la placa |

## 5. Etapas de audio (orientativas)

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
- PWM en GP10 con portadora alta (cientos de kHz).
- Filtro RC de 2 etapas con corte en torno a 3-4 kHz (por ejemplo 1 kΩ y 47 nF).
- Condensador de acople de 10 µF hacia el jack de SALIDA.
- Un puente para elegir nivel de línea o de micrófono (divisor ~100:1) cuando el destino es la entrada de micro de un móvil.
- Desde el mismo punto, al PAM8302 y al altavoz, o un piezo directamente desde el PWM con una resistencia en serie.

El RP2350 no tiene DAC. Por eso la salida es PWM filtrado, más que suficiente para DTMF.

## 6. Niveles eléctricos (a documentar, no a resolver)

El núcleo trabaja a **3,3 V**. Cada entusiasta adapta su aparato. Lo que hay que saber:

- **Pines digitales del RP2350:** según fuentes secundarias, toleran hasta 5,5 V **solo con el chip alimentado**. Durante el arranque pueden dañarse. Para PS/2, user port del C64 y similares, se recomienda un adaptador de niveles (BSS138, ~1 $) o como mínimo resistencias en serie. *Pendiente de confirmar en el datasheet del RP2350.*
- **Pines del ADC (GP26-GP28): nunca más de 3,3 V.** No toleran 5 V.
- **RS-232 (±12 V):** necesita siempre un MAX3232 o equivalente. Conectarlo directo destruye el pin.
- **Teclados PS/2:** piden 5 V de alimentación. Con 3×AA (4,5 V) algunos funcionan y otros no. Alternativa: alimentar la Pico por USB desde un power bank y sacar los 5 V de VBUS para el teclado. El firmware no usa los datos USB.
- **VSYS:** acepta 1,8-5,5 V. Con 3×AA se usa VSYS directamente; con USB, una Schottky en serie con las pilas, como indica la documentación de la Pico.

## 7. Modelo de confianza

| Tipo de periférico | Ejemplos | Dentro de la frontera de confianza | Puede ver la seed |
|---|---|---|---|
| **Pasivo** (lo controla el firmador) | Monitor VGA, tele, LCD I2C, LEDs | Sí | Sí, solo durante su introducción |
| **Entrada directa** | Teclado PS/2, teclado 4×4, botones | Sí | Sí, es por donde entra |
| **Inteligente** (tiene su propio procesador y software) | Terminal por UART: C64, HP-48, Palm, PC | **No** | **Nunca** |
| **Canal de audio** | Móvil, ordenador, magnetófono | No | Solo la seed cifrada con PIN de ur-tones |

Reglas:
1. **La seed nunca entra ni sale por UART.** Solo por DTMF (recomendado con cable; por el aire, siempre con PIN) o por un teclado conectado directamente.
2. **Firmar requiere pulsar CONFIRMAR en el propio firmador.** Ningún terminal puede firmar por su cuenta.
3. **Sin estado:** la seed vive solo en RAM y se pierde al apagar. Nada se escribe en flash.
4. **USB desactivado al arrancar.** Solo se activa manteniendo CANCELAR pulsado al encender (modo desarrollo: actualizar y releer el firmware para verificarlo).
5. **Nonces deterministas** (RFC 6979 para ECDSA; BIP340 sin aleatoriedad auxiliar obligatoria). No depende de ningún generador de aleatorios.
6. Cuando la transacción se revisa en un terminal UART, ese terminal es el que la verifica. Debe documentarse así para el usuario.

## 8. Protocolo UART (esbozo)

- 8N1, **9600 baudios por defecto**, configurable hasta 115200.
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

## 9. Módulos de firmware

| Módulo | Contenido |
|---|---|
| `audio` | Goertzel sobre el ADC y síntesis DTMF por PWM |
| `urtones` | Tramas, Reed-Solomon, fountain codes (reutilizar `c/ur_tones.c`) |
| `crypto` | libsecp256k1, SHA-256/512, BIP39, BIP32 |
| `psbt` | Parser BIP174, sighash segwit v0 (BIP143) y Taproot (BIP341) |
| `ui_uart` | Terminal de texto |
| `ui_vga` | Modo texto en VGA/DVI |
| `ui_lcd` | LCD de caracteres I2C |
| `input` | Botones, PS/2, teclado 4×4 |
| `system` | Arranque, desactivar USB, modo desarrollo, batería |

## 10. Pendiente de verificar

- Tolerancia a 5 V del RP2350 en su datasheet.
- Ruido del ADC de la Pico 2 con la etapa de micrófono propuesta.
- Tiempos de tono más cortos que los de ur-tones v0 con Goertzel.
- Compatibilidad de teclados PS/2 con pull-ups a 3,3 V y alimentación de 4,5 V.
- Rendimiento de libsecp256k1 y PBKDF2 (BIP39) en el RP2350.
- Errata del RP2350 sobre pull-downs internos: usar pull-ups para los botones.

## Referencias

- [ur-tones](https://github.com/ndssigner/ur-tones) y su [especificación](https://github.com/ndssigner/ur-tones/blob/main/SPEC.md)
- [pico-signer](https://github.com/notemilo/pico-signer): firmador sobre Pico 2 con lector QR por UART (MIT)
- [Raspberry Pi Pico 2](https://www.raspberrypi.com/products/raspberry-pi-pico-2/)
