# DTMF-Signer mínimo — Especificación de hardware (borrador)

**Estado:** propuesta de diseño, sin probar en hardware.
**Plataforma:** Raspberry Pi Pico 2 (RP2350), bare-metal, sin sistema operativo.
**Perfil:** este documento describe el perfil `PROFILE-PICO-FULL` de la [especificación](es/especificacion.md) y el [catálogo](es/catalogo.md). El prototipo actual (`PROFILE-PICO-REF`) usa una Pico RP2040.

---

## 1. Filosofía

- **La idea central: la seed llega como tonos y nunca se muestra.** Se genera en otro sitio y [ur-tones](https://github.com/ndssigner/ur-tones) la convierte en tonos DTMF, con PIN o sin él, desde un ordenador desconectado. El firmador la recibe por audio y no la muestra jamás, por ningún canal: solo su huella. A partir de ahí, **el usuario elige** cómo interactuar: todo por audio, para el máximo aislamiento, o con un terminal vintage por UART.
- **Un firmador básico que funciona solo.** Pantalla, cruceta y audio integrados. Cualquiera puede cargar una seed, revisar una transacción y firmarla sin nada más.
- **Mejor aún con una interfaz prestada.** El firmador vive mejor conectado a un aparato vintage (Game Boy, HP-48, C64…) que ponga pantalla y teclado. Esa vía se ofrece y se documenta, pero nunca es obligatoria.
- **Montar y listo.** El proyecto ofrece una PCB y un diseño de protoboard. El usuario solo flashea la Pico 2 y la pincha en su zócalo.
- **Lo opcional, previsto pero sin montar.** Los puertos para entusiastas (RS-232, PS/2, vídeo, salida de audio por cable, pilas) vienen como agujeros con su circuito ya diseñado. Quien los quiera, suelda las piezas que indica la serigrafía.
- **Sin cajas negras.** Todo el camino de la señal es código propio y auditable. No hay módulos con firmware cerrado (lectores QR, cámaras); para firmar con QR ya existen otros proyectos.
- **Comunicación airgapped por audio** con [ur-tones](https://github.com/ndssigner/ur-tones): por el aire para lo público y por cable para lo sensible.
- **Para entusiastas.** Documentamos bien las interfaces; adaptar alimentaciones y niveles de otros aparatos es cosa de la comunidad.

## 2. Placa básica (montada de serie)

| Componente | Función | Coste aprox. |
|---|---|---|
| Raspberry Pi Pico 2 en zócalo (2 tiras hembra de 20 pines) | MCU | 5,5 $ |
| OLED 128×64 de 1,3" por I2C (SH1106 o SSD1306) | Pantalla | 3-4 $ |
| Cruceta de 5 botones (4 direcciones + central) en escalera de resistencias | Navegar, teclear, confirmar | 0,5 $ |
| Micrófono electret + MCP6001 + filtro paso banda ancho | Recepción por el aire | 1 $ |
| Piezo excitado directamente por la Pico | Emisión por el aire | 0,3 $ |
| Jack TRS de 3,5 mm **solo de entrada**, con contacto de detección, y su etapa de línea | Canal por cable para datos sensibles | 0,5 $ |
| USB-C solo alimentación + 2 × 5,1 kΩ + Schottky | Alimentación | 0,7 $ |
| Carga de mantenimiento (2N7000 + 33 Ω 1 W) | Que los power banks no se apaguen | 0,3 $ |
| PCB (tirada pequeña) | | ~2 $ |
| **Total** | | **~13,5 $** |

## 3. Agujeros con circuito previsto (sin montar)

| Puerto | Piezas a soldar | Notas |
|---|---|---|
| Serie TTL | Tira de 4 pines | 3,3 V, en los mismos pines que el RS-232: **uno u otro** |
| Serie RS-232 | MAX3232 + 4 condensadores + DE-9 macho | Nunca conectar RS-232 directo a la Pico |
| Teclado PS/2 | Mini-DIN 6 hembra + 2 BSS138 + resistencias | El teclado necesita 5 V: hay 5 V con USB-C; con pilas, cada uno se apaña |
| Vídeo compuesto B/N | RCA hembra + 2 resistencias | Tele o monitor como pantalla pasiva |
| Salida de audio por cable | Jack, bornera o cables + filtro RC | Para quien quiera emitir por cable |
| Pilas | Portapilas a elegir + Schottky | 2×AAA recomendado; CR2032 experimental |
| Ampliación | Tira con GPIO libres, 3V3, 5V y GND | GP12-GP19 coinciden con el bloque HSTX (DVI, VGA…) |

## 4. Alimentación

- **De serie: USB-C solo alimentación**, desde el cargador del móvil o un power bank.
  - Necesita **dos resistencias de 5,1 kΩ en CC1 y CC2**; sin ellas, los cargadores USB-C a USB-C no dan tensión. La regulación la hace la fuente.
  - **Las líneas de datos no se conectan a nada**: aislamiento por hardware aunque se enchufe a un ordenador.
  - Del raíl de 5 V salen VSYS (a través de una Schottky) y los 5 V de los agujeros de PS/2 y de ampliación.
  - **Desenchufar = apagar = seed perdida.** No hace falta interruptor. La RAM pierde su contenido sin alimentación, aunque no al instante; por eso el firmware la pone a cero antes: al terminar, al cancelar y por inactividad.
- **Carga de mantenimiento:** el firmware genera un pulso de ~150 mA durante ~100 ms cada ~10 s, para que los power banks no se apaguen por bajo consumo. Duración, intervalo y activación configurables; con cargador de pared se desactiva.
- **Micro-USB de la Pico: solo para flashear.** En caso de apuro, sirve para alimentarla desde un cargador, nunca desde un ordenador: ese puerto sí tiene datos.
- **Pilas (opcional, agujeros):** a VSYS a través de una Schottky. Recomendado 2×AAA (~200-250 sesiones de 5 min, estimado). La CR2032 es una variante experimental: a 25-45 mA su tensión cae y puede apagarse a mitad de sesión. No es peligroso, porque no hay estado que perder, pero sí molesto.
- **Consumo estimado en uso: ~25-45 mA.** *Pendiente de medir.*

## 5. Asignación de pines

| GPIO | Uso | Notas |
|---|---|---|
| GP0 / GP1 | UART0 TX / RX | A la tira TTL o al MAX3232 (agujeros) |
| GP2 / GP3 | Piezo | PWM en contrafase (mismo slice, salidas A y B): dobla la tensión |
| GP4 / GP5 | I2C0 SDA / SCL | OLED |
| GP6 | Carga de mantenimiento | Puerta del 2N7000 |
| GP7 | Libre (ampliación) | |
| GP8 | Detección de cable en el jack de ENTRADA | Contacto de conmutación del jack |
| GP9 | Libre (ampliación) | |
| GP10 / GP11 | PS/2 datos / reloj (agujeros) | PIO, a través de BSS138 |
| GP12-GP19 | Libres (ampliación) | Bloque HSTX del RP2350 |
| GP20 | Salida de audio por cable (agujeros) | PWM + filtro RC |
| GP21 / GP22 | Compuesto: sincronía / vídeo (agujeros) | PIO |
| GP25 | LED de la placa | Actividad |
| GP26 (ADC0) | Micrófono | **Máx. 3,3 V** |
| GP27 (ADC1) | Línea del jack de ENTRADA | **Máx. 3,3 V** |
| GP28 (ADC2) | Cruceta (escalera de resistencias) | **Máx. 3,3 V** |
| GP29 (ADC3, interno) | VSYS/3 | Ya cableado en la placa |

## 6. Audio

Como el micrófono y la línea entran por pines distintos del ADC, y el jack tiene contacto de detección, **el firmware sabe siempre por dónde llega cada dato**.

**Filtrado analógico: un paso banda ancho de ~300 Hz a ~4 kHz** en las dos entradas.
- El paso alto elimina la continua, el zumbido de red, los golpes y el viento. Sale de los condensadores de acople y la polarización.
- El paso bajo es el **filtro antialiasing**, imprescindible. El ADC puede muestrear muy por encima de lo necesario y filtrar luego en digital, así que basta un filtro sencillo.
- **No se usan filtros estrechos por cada tono DTMF.** Goertzel ya es un filtro mucho más estrecho. Los analógicos estrechos resuenan y emborronan los tonos cortos, piden componentes precisos y atarían la placa a DTMF para siempre.

**Entrada por micrófono:**
- Electret polarizado con 2,2 kΩ a 3V3.
- Condensador de acople de 1 µF.
- Amplificador no inversor con ganancia de 20 a 50, centrado a 1,65 V con un divisor de 2×10 kΩ.
- Filtro paso bajo antes de GP26.

**Entrada de línea (jack de ENTRADA):**
- Se conecta con un cable macho-macho normal desde la salida de auriculares del aparato de origen. Los móviles sin jack necesitan un adaptador USB-C a 3,5 mm.
- Condensador de acople de 1 µF, centrado a 1,65 V con 2×10 kΩ, resistencia de 1 kΩ en serie como protección, y filtro paso bajo antes de GP27.
- Si el aparato de origen da más nivel del que admite el ADC, un divisor en la entrada.

**Emisión por el aire (piezo):**
- Excitado directamente desde GP2 y GP3 en contrafase, con una resistencia en serie. No necesita amplificador.
- El piezo suena mucho más fuerte hacia 1,5 kHz que hacia 700 Hz. **El firmware compensa dando más amplitud al grupo bajo de tonos.**
- **Uso: aparatos cerca (unos centímetros) y volumen moderado.** A 1-2 cm puede saturar el micrófono del móvil. La documentación debe indicar dónde suele estar el micrófono (borde inferior en móviles, junto a la webcam en portátiles).

**Robustez por firmware:** comprobar la energía de los tonos frente a la total, el *twist* entre los dos tonos, los segundos armónicos y la duración mínima, además de la corrección Reed-Solomon de ur-tones.

**Para la herramienta web de ur-tones:** abrir el micrófono con `echoCancellation`, `noiseSuppression` y `autoGainControl` a `false`, porque esos procesados están pensados para voz.

## 7. Interfaz integrada

**Pantalla OLED 128×64:** 21 columnas × 8 líneas. El firmware debe soportar SH1106, SSD1306 y SSD1315, que circulan mezclados. **El orden de pines del módulo varía según el fabricante** (VCC-GND o GND-VCC): puente de soldadura para elegir y advertencia en la serigrafía.

**Cruceta de 5 botones por ADC:** cada botón cierra a masa una resistencia distinta frente a una pull-up de 10 kΩ. Valores orientativos: 0 Ω, 2,2 kΩ, 4,7 kΩ, 10 kΩ y 22 kΩ, que dan ~0 V, 0,6 V, 1,06 V, 1,65 V y 2,27 V; 3,3 V sin pulsar. No detecta pulsaciones simultáneas, y no hace falta.

**Revisar y firmar una transacción:**

```
[1/5] RESUMEN            [2/5] SALIDA 1/2        [3/5] SALIDA 2/2
2 salidas                bc1q 9h7g 2x4k ...      CAMBIO (verificado)
Envío: 0.01500000        ... (4 líneas)          0.00480000
Comisión: 1200 sat       0.01500000 BTC
(3.1 sat/vB)

[4/5] COMISIÓN           [5/5] ¿FIRMAR?
1200 sat                 Mantén central 2 s: firmar
3.1 sat/vB               Mantén izquierda 2 s: cancelar
```

- Las direcciones se muestran completas, en grupos de 4 caracteres. Una Taproot de 62 caracteres ocupa 4 líneas.
- **La opción de firmar solo aparece después de pasar por todas las pantallas.** No hay atajo.
- **Firmar exige una pulsación larga de 2 s.**
- El cambio se marca "verificado" solo si el firmador comprueba que pertenece a la seed cargada.

**PIN (A-Z, 0-9):** rejilla de 6×6, que coincide con los 36 símbolos admitidos por ur-tones.

```
 A B C D E F
 G H I J K L
 M N O P Q R
 S T U V W X
 Y Z 0 1 2 3
 4 5 6 7 8 9      PIN: ••••Q_ _ _ _ _ _ _
```

- Las flechas mueven el cursor, que da la vuelta por los bordes, y el central añade el carácter. Pulsación larga del central: terminar; de la izquierda: borrar.
- **Enmascarado por defecto**, salvo el último carácter. El enmascarado se puede desactivar en los ajustes.
- Un PIN de 12 caracteres, el mínimo que recomienda ur-tones para seeds enviadas por el aire, son ~50 pulsaciones, alrededor de un minuto.

**Passphrase:** 3 páginas de 6×6 (mayúsculas y dígitos; minúsculas y dígitos; símbolos ASCII y espacio), que se cambian con pulsación larga hacia arriba. 20 caracteres son unas 100-120 pulsaciones. **Enmascarada por defecto** salvo el último carácter, con opción de desactivarlo. **Recomendar passphrases solo ASCII:** sin ñ ni tildes, porque el teclado no las tiene y BIP39 las normaliza (NFKD) de forma no siempre igual entre carteras.

**Huellas:** tras cargar la seed y el PIN se muestra la **huella de la seed**. Tras la passphrase, la **huella de la cartera final**. Un PIN o una passphrase con una letra mal da otra cartera válida y vacía, así que estas comprobaciones son imprescindibles.

**La seed nunca se muestra.** Ni en el OLED, ni por vídeo, ni por UART, ni por audio. No existe ninguna pantalla para introducirla a mano ni para revisarla: solo entra por audio (o por UART con confirmación) y solo se muestra su huella. Así, un implante que lea la pantalla (el caso Ledger de octubre de 2026), una cámara o alguien mirando no tienen nada que capturar.

**Matiz sobre el PIN y la passphrase con la cruceta:** la rejilla y su cursor se ven en la pantalla. Aunque el texto esté enmascarado, un implante que lea la pantalla podría reconstruirlos viendo qué casilla se elige. Por sí solos no sirven sin la seed, que nunca pasa por la pantalla. Para el máximo nivel, el PIN y la passphrase también por cable.

## 8. Política de canales

La política de canales es común a todos los montajes y está en la [especificación](es/especificacion.md#4-canales): qué dato se acepta por qué clase de canal, y por qué. En esta placa, cada puerto pertenece a una clase:

| Puerto | Clase |
|---|---|
| Micrófono (GP26) | `AIR` |
| Jack de ENTRADA (GP27, con detección en GP8) | `CABLE` |
| Serie TTL o RS-232 (GP0/GP1) | `CABLE` |
| Cruceta, teclado PS/2 | `LOCAL` |
| Piezo (salida) | `AIR` |
| Salida de audio por cable | `CABLE` |

- Como el micrófono y la línea entran por pines distintos (§6), el firmware sabe siempre la clase de cada dato.
- Los datos sensibles que llegan por UART solo se aceptan tras pulsar el botón central.
- La confirmación física de la firma es la pulsación larga del central (§7).

**Uso cotidiano recomendado:**
1. La seed se genera fuera del firmador (con dados, o con otra cartera de confianza).
2. En un **ordenador desconectado**, la herramienta web de ur-tones la convierte en tonos, protegida con un **PIN de 12 caracteres o más** (la herramienta puede generarlo), y la guarda como **WAV**.
3. El PIN se apunta en papel o en un gestor de contraseñas, **siempre separado del WAV**.
4. El WAV se guarda en un **reproductor de audio sin conexión a internet** (un reproductor MP3, por ejemplo).
5. Para firmar: el reproductor se conecta **por cable** al jack de entrada y reproduce el WAV; el PIN se teclea en el firmador; se comprueba la huella.
6. A partir de ahí, las PSBT van y vienen por el aire con la cartera, o por UART con un terminal vintage.

El WAV es una copia de la seed cifrada: quien lo consiga puede probar PINs sin conexión, por eso el PIN debe ser largo. Y **no debe ser la única copia de seguridad**: los reproductores se estropean. La seed original se conserva por otra vía (papel, metal, [SeedCraft](https://github.com/ndssigner/seedcraft)…).

## 9. Interfaz prestada por aparatos vintage

- **Protocolo serie de texto ASCII**, en líneas terminadas en CRLF, 8N1, 9600 baudios por defecto y configurable hasta 115200. **Cualquier programa de terminal existente** sirve sin software específico; la comunidad puede escribir después aplicaciones nativas.
- PSBT de entrada y salida en texto UR (`ur:crypto-psbt/...`), el mismo formato que ur-tones, o en base64.
- **Aunque haya terminal, la confirmación final siempre se hace en el firmador**, y el OLED muestra también el resumen de lo que se firma. El terminal es una comodidad; la referencia de confianza sigue siendo el firmador.
- Otros aparatos (link de Game Boy, buses propios) por PIO en los GPIO libres.
- Ejemplo de diálogo:

```
> PSBT recibida: 2 salidas
  1) bc1q...xyz   0.01500000
  2) cambio       0.00480000
  comisión: 1200 sat
Pulse el botón central del firmador para firmar
```

La especificación completa del protocolo es un documento aparte, pendiente.

## 10. Vídeo compuesto (opcional)

- **Blanco y negro**, sin portadora de color. La misma señal sirve para PAL y NTSC; el firmware elige 50 o 60 Hz.
- **Dos resistencias** desde GP21 (sincronía) y GP22 (vídeo) hasta el RCA, sobre la carga de 75 Ω de la tele. Valores orientativos: ~680 Ω y ~220-330 Ω, para ~0 V de sincronía, ~0,3 V de negro y ~1 V de blanco. *A ajustar en el prototipo.*
- **Modo texto de 40 columnas**, legible en una tele de tubo.
- **La seed nunca se muestra**, así que por vídeo no puede filtrarse. Lo que sí se ve es la rejilla del PIN y la passphrase.
- **Smart TV:** pueden analizar y enviar lo que muestran (ACR), por cualquier entrada. Si se usa una tele conectada a internet, el PIN y la passphrase mejor por cable.
- **Las señales de vídeo analógicas pueden captarse a distancia** con equipo especializado (*van Eck phreaking*). Afecta, como mucho, a la rejilla del PIN y la passphrase.

## 11. Niveles eléctricos

Los puertos de la placa traen su protección prevista: el MAX3232 para RS-232 y los BSS138 para PS/2. Para lo que se conecte por la tira de ampliación, cada entusiasta adapta su aparato. Lo que hay que saber:

- **Pines digitales del RP2350:** según fuentes secundarias, toleran hasta 5,5 V **solo con el chip alimentado**. Durante el arranque pueden dañarse. Se recomienda adaptador de niveles o resistencias en serie. *Pendiente de confirmar en el datasheet.*
- **Pines del ADC (GP26-GP28): nunca más de 3,3 V.**
- **RS-232 (±12 V):** siempre con un MAX3232 o equivalente.

## 12. Reglas de seguridad

1. **La seed nunca se muestra** por ningún canal. Solo su huella.
2. **Sin estado:** la seed vive solo en RAM. Nada se escribe en flash. El firmware la pone a cero al terminar, al cancelar y por inactividad; desenchufar también la pierde, aunque no al instante (ver la [§8 de la especificación](es/especificacion.md#8-guardar-la-semilla-y-borrado-de-emergencia)).
3. **Apagado automático por inactividad**, que borra la seed.
4. **Firmar requiere la cruceta del propio firmador.** Ningún terminal puede firmar por su cuenta.
5. **Política de canales** de la sección 8, impuesta por el firmware.
6. **USB de la Pico desactivado al arrancar.** Solo se activa manteniendo la flecha izquierda pulsada al encender (modo desarrollo: actualizar y releer el firmware para verificarlo).
7. **Nonces deterministas** (RFC 6979 para ECDSA; BIP340 sin aleatoriedad auxiliar obligatoria). No depende de ningún generador de aleatorios.
8. **Alimentarse de un cargador o un power bank, no de un ordenador.** Aunque el USB-C no tenga datos, un equipo comprometido podría en teoría medir el consumo mientras se firma (análisis de potencia).

## 13. Formatos de distribución

El objetivo es **abaratar y facilitar la vida al público**, no hacer negocio. El análisis de riesgos de cada formato (manipulación, cadena de suministro y **privacidad del comprador**) está en [analisis-riesgos.md](analisis-riesgos.md).

| Formato | Contenido | Para quién |
|---|---|---|
| **Protoboard + lista de materiales** | Esquema de cableado, fotos y lista de módulos comerciales comunes (Pico 2, OLED, módulo de micrófono, piezo, botones, adaptadores de jack y USB-C) | Entusiastas y prototipado |
| **Placa perforada + guía de montaje** | Los mismos módulos que en la protoboard, **soldados** en una placa perforada con el trazado de una protoboard. Pico y OLED en zócalo de tiras hembra | Quien quiere algo sólido y portátil sin pedir una PCB |
| **PCB "shell"** | Placa montada **sin la Pico ni el OLED**, ambos en zócalo. El usuario los compra por su cuenta y flashea la Pico | La mayoría |
| **PCB con Pico y OLED** | La misma placa con la Pico **sin flashear** y el OLED, **ambos en zócalo** | Quien prioriza la comodidad |
| **PCB de agujero pasante, vacía** | Placa que el usuario pide (o se fabrica) y suelda entera por su cuenta | Los más exigentes |

**Placa perforada recomendada:** una placa de prototipos **con el mismo trazado que una protoboard** (filas de 5 puntos unidos y canal central; se venden como *solderable breadboard* o *Perma-Proto*), de **tamaño completo**, con agujeros metalizados por las dos caras. Ventajas:
- **El mismo esquema sirve para protoboard y placa perforada:** cada pieza va en la misma fila y columna. Se prueba en la protoboard y, cuando funciona, se pasa tal cual a la placa perforada.
- La Pico 2 está diseñada para montarse a caballo del canal central, como en una protoboard. En la placa perforada va sobre **tiras hembra**, para poder sacarla.
- **Sólida y portátil**, más que una protoboard, y sin pedir una PCB a nadie: es una pieza genérica, con la misma privacidad que el formato protoboard.
- Solo pide soldar componentes de agujero pasante, lo más sencillo de soldar.
- Alternativa más barata: **placa de tiras** (*stripboard*, tipo Veroboard), con pistas de cobre continuas que hay que cortar en ciertos puntos. Necesita su propio esquema de colocación; queda para la comunidad.

*Pendiente:* comprobar en el esquema que todo cabe en una placa de tamaño completo, y elegir modelos concretos con disponibilidad actual. El esquema de montaje y cableado se publicará junto con el de protoboard.

**Dos diseños de PCB, un mismo circuito:**

| Diseño | Componentes | Base de |
|---|---|---|
| **PCB-SMD** | Pasivos y semiconductores pequeños en SMD de tamaño grande (0805/1206), fáciles de inspeccionar, **montados por el fabricante**. Conectores y zócalos de agujero pasante | PCB "shell" y PCB con Pico y OLED |
| **PCB-THT** | **Todo de agujero pasante**, pensado para soldar a mano | PCB de agujero pasante, vacía |

- Mismo esquema eléctrico, misma asignación de pines y mismo firmware. Solo cambia el diseño físico de la placa.
- El montaje en fábrica es mucho más barato en SMD; el soldado a mano es mucho más fácil en agujero pasante. Por eso dos diseños.

Notas:
- **PCB "shell":** se publicarán **fotos de referencia en alta resolución** de la placa montada para poder compararla.
- **PCB con Pico y OLED:** el usuario debe **reflashear siempre** la Pico con el firmware oficial verificado, aunque venga flasheada, y revisar el OLED contra las fotos de referencia.
- **Normativa:** vender aparatos electrónicos ya montados puede requerir marcado CE (UE), FCC (EE. UU.), RoHS y gestión de residuos electrónicos. *Pendiente de consultar antes de ofrecer los formatos montados.*

**Financiación:** PCBWay paga una **comisión del 10 % por pedido** de los proyectos compartidos en su comunidad. *Pendiente de confirmar las condiciones actuales y si se aplica a placas ensambladas.* Otras vías: Tindie (venta directa de kits) o Crowd Supply (campañas en las que ellos fabrican y distribuyen). Los archivos se publican también en el repositorio, para no depender de un único fabricante.

**Licencia y nombre:**
- Propuesta de licencia de hardware: **CERN-OHL-S v2**, que obliga a que las versiones modificadas también sean abiertas.
- Al ser hardware abierto, cualquiera puede fabricarlo y venderlo sin dar nada al proyecto; es parte del trato. Lo que distingue la versión oficial es la confianza: enlaces desde el repositorio, fotos de referencia y versiones firmadas. Valorar registrar el nombre como marca para diferenciarla de clones.

**Diseño:** KiCad, publicado como hardware abierto. *Pendiente.*

## 14. Módulos de firmware

| Módulo | Contenido |
|---|---|
| `audio` | Goertzel sobre el ADC, síntesis DTMF por PWM, compensación del piezo, validación de tonos |
| `urtones` | Tramas, Reed-Solomon, fountain codes, PIN (reutilizar `c/ur_tones.c`) |
| `crypto` | libsecp256k1, SHA-256/512, BIP39, BIP32 |
| `psbt` | Parser BIP174, sighash segwit v0 (BIP143) y Taproot (BIP341) |
| `policy` | Política de canales: qué dato se acepta por dónde |
| `ui_oled` | Pantallas de revisión, rejillas de PIN y passphrase, huellas |
| `input` | Cruceta por ADC; PS/2 por PIO (opcional) |
| `serial` | Protocolo de texto por UART |
| `video` | Compuesto B/N por PIO (opcional) |
| `system` | Arranque, desactivar USB, modo desarrollo, carga de mantenimiento, apagado por inactividad |

## 15. Validación en hardware virtual

Antes de publicar el repositorio con los diseños, probar en simulación todo lo que se pueda. Herramientas candidatas, *pendientes de verificar*:

| Qué | Herramienta candidata |
|---|---|
| Etapas analógicas (micrófono, línea, filtros, escalera de la cruceta, vídeo compuesto) | Simulación SPICE (ngspice, integrado en KiCad) |
| Firmware con Pico, OLED, botones y piezo | Wokwi (simulador en línea con soporte de Pico). Comprobar si admite el RP2350 y entradas analógicas de audio |
| Decodificador Goertzel y ur-tones | Pruebas nativas en el ordenador alimentadas con archivos WAV, grabados y sintéticos con ruido |
| Emulación más completa del microcontrolador | Renode u otros emuladores. Comprobar soporte del RP2350 |

## 16. Pendiente de verificar

- Alcance y fiabilidad del piezo y del micrófono con móviles y portátiles reales.
- Ruido del ADC de la Pico 2 con las etapas de micrófono y línea propuestas.
- Tiempos de tono más cortos que los de ur-tones v0 con Goertzel.
- Valores de la escalera de resistencias de la cruceta.
- Consumo total y parámetros de la carga de mantenimiento con distintos power banks.
- Tolerancia a 5 V del RP2350 en su datasheet.
- Valores de las resistencias del compuesto y estabilidad de la imagen en teles PAL y NTSC.
- Rendimiento de libsecp256k1, PBKDF2 (BIP39) y del PIN de ur-tones en el RP2350.
- Errata del RP2350 sobre pull-downs internos: usar pull-ups.

## 17. Ideas para el futuro (no prioritarias)

### Entrada de seeds de SeedCraft por consola

Para quien guarde su seed con [SeedCraft](https://github.com/ndssigner/seedcraft) (una secuencia de objetos, por ejemplo cuentas de un collar), poder introducirla en el firmador **en modo consola**: teclado PS/2 con tele, o terminal por UART. Así la seed iría del collar al firmador sin pasar por ningún ordenador. No por DTMF: para eso ya está la seed codificada con ur-tones.

1. **Primero el diccionario.** El usuario "bautiza" cada símbolo, en orden, con un nombre y una abreviatura:

   ```
   0: Bola roja (BR)
   1: Tubo negro (TNG)
   ...
   15: ...
   ```

   El firmador comprueba que hay N entradas (16 en el tamaño recomendado) y que no se repite ninguna abreviatura.
2. **Después la secuencia**, tecleando la abreviatura de cada objeto según se lee el collar. Es más fácil y menos propenso a errores que traducir mentalmente cada objeto a su número.
3. **Respetando la regla de no mostrar nunca la seed.** En el modo sin llave, la secuencia equivale a la seed en claro, así que **no se muestra lo tecleado**: solo el progreso ("Pieza 23/54") y un aviso si la abreviatura no existe en el diccionario. La entrada a ciegas tiene red de seguridad: el QR de SeedCraft repara una pieza mal leída, el firmador puede avisar por filas de 16 piezas y al final muestra la huella.
4. **Modo con llave:** se introducen las dos secuencias y el firmador las combina internamente.

Detalles de la entrada:
- **Separador entre piezas** (espacio o Enter) y **sin distinguir mayúsculas**, para que abreviaturas como "T", "TN" y "TNG" puedan convivir sin ambigüedad.
- **Abreviaturas solo con letras y dígitos ASCII.** Los teclados PS/2 envían la posición de la tecla, no el carácter, y el firmware tiene que suponer una distribución (inglesa por defecto, española como opción). Los nombres largos pueden llevar cualquier carácter: solo sirven para bautizar.
- **Cada abreviatura se valida al momento:** si no existe en el diccionario, el firmador avisa en el acto, aunque no muestre lo tecleado.
- **Comando para deshacer la última pieza.**
- **Introducción por filas de 16 piezas**, con confirmación al terminar cada fila. Una pieza de más o de menos desplaza todo lo que viene detrás; por filas se localiza enseguida dónde está el fallo.

Notas:
- La secuencia de SeedCraft cuenta como **seed sin cifrar** en la política de canales: por PS/2 o por UART con confirmación, nunca por el aire.
- **Vía recomendada: teclado PS/2 con tele**, ambos dentro de la frontera de confianza. Por UART, el terminal ve el diccionario y la secuencia, es decir, la seed completa: mejor un aparato que nunca se haya conectado a la red.
- El diccionario se introduce en cada sesión: el firmador no guarda nada.
- Requiere portar a C el decodificador de SeedCraft (de símbolos a módulos del QR, quitar la máscara, corregir errores y extraer la seed).

## Referencias

- [ur-tones](https://github.com/ndssigner/ur-tones) y su [especificación](https://github.com/ndssigner/ur-tones/blob/main/SPEC.md)
- [pico-signer](https://github.com/notemilo/pico-signer): firmador sobre Pico 2 con lector QR por UART (MIT)
- [Raspberry Pi Pico 2](https://www.raspberrypi.com/products/raspberry-pi-pico-2/)
