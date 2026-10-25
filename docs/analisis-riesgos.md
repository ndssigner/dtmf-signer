# DTMF-Signer — Análisis de riesgos (borrador)

**Estado:** borrador vivo. Acompaña a [hardware-minimo.md](hardware-minimo.md).
**Valoraciones:** cualitativas (baja / media / alta), sin pruebas en hardware ni auditoría externa.

---

## 1. Qué protegemos y de quién

**Activos:**
- La **seed**, el **PIN** y la **passphrase**.
- La **integridad de lo que se firma**: que el usuario firme exactamente la transacción que cree firmar.
- La **identidad del usuario como poseedor de bitcoin**: su nombre, su dirección postal y la relación entre ambos y el firmador.

**Adversarios considerados:**
- Alguien cercano que escucha o graba el audio.
- Un ordenador, móvil o terminal comprometido.
- Un fabricante, distribuidor o transportista malicioso o comprometido.
- Quien obtenga una base de datos de clientes filtrada.
- Un atacante con acceso físico al firmador o al usuario (incluida la coacción).

**Fuera de alcance:** atacantes con laboratorio para atacar el chip (inyección de fallos, lectura invasiva). Como el firmador no guarda nada, esos ataques solo tendrían sentido durante una sesión.

## 2. Riesgos de distribución y privacidad

### 2.1 Por formato

| Riesgo | Protoboard + lista de materiales | PCB vacía | PCB "shell" (sin Pico ni OLED) | PCB con Pico y OLED |
|---|---|---|---|---|
| Hardware manipulado | **Muy baja**: piezas genéricas, el vendedor no sabe para qué son | **Baja**: placa sin componentes, fácil de inspeccionar | **Media-baja**: el fabricante monta piezas; pocas y visibles | **Media**: la Pico o el OLED podrían venir sustituidos o modificados |
| Firmware manipulado | Baja: lo flashea el usuario | Baja | Baja | **Alta si se usa tal cual**; baja si el usuario la reflashea |
| Pico o chip falsificados | Baja si se compra en distribuidor oficial | Baja | Baja (la compra el usuario) | Media: depende de dónde la obtuvo el fabricante |
| **Vinculación de identidad** | **Baja**: millones de aficionados compran estas piezas | Media: el pedido lleva el nombre del proyecto | **Alta**: pedido identificable con dirección postal | **Alta**: además, es claramente un producto final |
| Interceptación dirigida del envío | Baja: varios paquetes genéricos | Baja | Media | Media-alta: paquete identificable y listo para usar |
| Habilidad necesaria | Media: cablear | Alta: soldar | Baja | Muy baja |

La **placa perforada** con el trazado de una protoboard tiene el mismo perfil que el formato protoboard (piezas genéricas, muy privado). Solo cambia la habilidad necesaria: media, porque hay que soldar componentes de agujero pasante.

### 2.2 Vinculación de identidad: el riesgo que más se olvida

Cualquier pedido identificable como "firmador de bitcoin" deja **nombre, dirección postal, correo y teléfono** asociados a la condición de posible poseedor de bitcoin en la base de datos del fabricante, de la tienda, de la pasarela de pago y del transportista. **Hay que asumir que esas bases de datos pueden filtrarse algún día.**

Precedente: en 2020 se filtró la base de datos de clientes de Ledger, con cientos de miles de direcciones postales. Siguieron campañas de phishing, extorsiones y amenazas de ataques físicos a los compradores.

Mitigaciones:
- **Preferir el formato protoboard o comprar las piezas genéricas por separado**, mezcladas con otras compras de electrónica. Una Pico y un OLED no delatan a nadie.
- **Nombres neutros en los listados del fabricante** (por ejemplo, "Pico audio I/O board") y **serigrafía sin referencias a bitcoin**. El repositorio explica qué es; la página de pedido no tiene por qué decirlo.
- **Entrega en punto de recogida o taquilla**, cuando sea posible.
- **No reutilizar** el correo o el teléfono de exchanges o cuentas relacionadas con bitcoin.
- El firmador **no guarda nada**: si alguien lo encuentra, solo sabe que es una placa con una Pico y una pantalla.

### 2.3 Manipulación e interceptación

Mitigaciones:
- **Fotos de referencia en alta resolución** de cada versión de PCB, para comparar.
- **La Pico y el OLED siempre en zócalo**, nunca soldados: se pueden sacar, inspeccionar y cambiar.
- **Revisar el módulo OLED** en los formatos que lo traen montado. El implante de los Ledger manipulados de octubre de 2026 leía precisamente la pantalla. En el OLED solo debe haber el panel, su controlador y unos pocos pasivos; cualquier chip de más es sospechoso. Para enviar lo que lee, un implante necesitaría además su propia radio y alimentación, porque el firmador no tiene ninguna conexión inalámbrica. Ante la duda, se cambia por un OLED comprado aparte.
- **Reflashear siempre** con el firmware oficial, venga como venga.
- **Comprar solo desde los enlaces del repositorio** para los formatos montados. Los clones de terceros son legítimos (hardware abierto), pero no hay forma de saber cómo se han montado.

## 3. Riesgos de firmware

| Riesgo | Valoración | Mitigación |
|---|---|---|
| Firmware malicioso descargado de una fuente falsa | Media | Versiones **firmadas** y **compilaciones reproducibles**; publicar los hashes |
| Firmware alterado en la flash de la Pico | Media | Releer la flash en modo desarrollo y comparar su hash; reflashear ante la duda |
| Errores de implementación criptográfica | Media | libsecp256k1 (tiempo constante, muy revisada); vectores de prueba; auditoría externa antes de la versión 1.0 |
| Generador de aleatorios débil | **No aplica** | Nonces deterministas (RFC 6979 y BIP340); el firmador no genera seeds |

## 4. Riesgos de uso

| Riesgo | Valoración | Mitigación |
|---|---|---|
| Alguien graba la seed cifrada que se envía por el aire y prueba PINs sin conexión | Media | PIN de **12 caracteres o más**; seed **por cable** cuando sea posible; el PIN nunca suena |
| El aparato que envía la seed o la passphrase está comprometido | Media | Enviar datos sensibles desde un aparato **desconectado de la red**; el cable protege el camino, no el origen |
| La cartera o el ordenador generan una PSBT con una dirección cambiada | Media | Revisión **completa en el OLED** antes de firmar; verificación de la salida de cambio |
| Un terminal UART comprometido muestra datos falsos o inyecta datos sensibles | Media | Resumen también en el OLED; **confirmación física** obligatoria para firmar y para aceptar datos sensibles |
| Las grabaciones de audio guardadas son copias de la seed | Media | Tratarlas como copias de seguridad en papel; cifradas con PIN largo |
| Pérdida o robo del reproductor con el WAV de la seed | Media | PIN de **12 caracteres o más** (ur-tones puede generar uno de 60 bits), guardado **separado** del WAV; el WAV no es la única copia de la seed |
| Implante que lee la pantalla (caso Ledger) | Baja | **La seed nunca se muestra**: no hay nada que capturar. La rejilla del PIN y la passphrase sí se ve; para el máximo nivel, PIN y passphrase por cable |
| Cámara o alguien mirando la pantalla | Baja | La seed nunca se muestra; PIN y passphrase enmascarados por defecto |
| Una passphrase cifrada guardada junto a la seed cifrada permite comprobar rápido si se acertó el PIN | Media | Passphrase por cable en directo o tecleada en el firmador; no guardarla cifrada junto a la seed |
| PIN o passphrase con un error dan otra cartera válida y vacía | Alta (error humano) | Comparar siempre las **huellas** que muestra el OLED |
| Se deja el firmador encendido con la seed cargada | Media | Apagado por inactividad que borra la seed; desenchufar = borrar |
| Análisis de potencia si se alimenta desde un ordenador comprometido | Baja | Alimentar desde cargador o power bank |
| Emisiones de la señal de vídeo analógica captadas a distancia | Baja | La seed nunca se muestra; como mucho se ve la rejilla del PIN y la passphrase |
| Smart TV que analiza lo que muestra (ACR) | Baja, si se usa la salida de vídeo | La seed nunca se muestra; con una tele conectada, PIN y passphrase por cable |
| Ruido o volumen inadecuado por el aire | Media (fallo, no fuga) | Corrección Reed-Solomon; aparatos cerca; cable como alternativa |
| Verificación con varios escuchadores que no son independientes de verdad (misma app, mismo sistema, o el aparato que creó la PSBT) | Media | Escuchadores de software o hardware distinto, sin conexión; el que creó la PSBT no cuenta como el honesto ([especificación, §5.1](es/especificacion.md#51-varios-escuchadores)) |
| Un teléfono recuperado usado como teclado guarda el PIN en su memoria de rellamada o en su agenda | Media | Solo el teclado, un teléfono sin memoria, o borrarla marcando otra cosa ([especificación, §4.3](es/especificacion.md#43-un-teclado-de-teléfono-recuperado)) |
| Un montaje que guarda la semilla la deja en una memoria de la que no se puede borrar con garantías | Media | Sin estado por defecto; si se guarda, cifrada y con borrado de emergencia que sobrescribe y comprueba ([especificación, §8](es/especificacion.md#8-guardar-la-semilla-y-borrado-de-emergencia)) |
| Un montaje sin importes comprobados con varias entradas firma una comisión enorme (ataque de la comisión en SegWit v0) | Media | El firmador muestra "comisión NO verificada" y el importe declarado de cada entrada; comprobarlos en otro aparato **antes de confirmar**, porque después es tarde: la PSBT firmada vuelve al aparato que la creó, que puede transmitirla él mismo; desconfiar si se pide firmar otra vez las mismas entradas ([especificación, §6](es/especificacion.md#6-el-ataque-de-la-comisión-en-segwit-v0)) |

## 5. Riesgos físicos

| Riesgo | Valoración | Mitigación |
|---|---|---|
| Coacción física para entregar los fondos ("ataque de la llave inglesa") | Baja, pero de impacto máximo | **Cartera señuelo**: la seed sin passphrase con una cantidad pequeña, y la principal tras la passphrase. Reducir la vinculación de identidad (sección 2.2) |
| Robo o incautación del firmador | Baja | No guarda nada; sin la seed, el PIN y la passphrase no sirve |
| Acceso físico al firmador durante una sesión | Baja | Sesiones cortas; apagado por inactividad |

## 6. Riesgos aceptados

Se documentan para que el usuario decida con conocimiento:

- **El aparato que envía la seed o la passphrase la conoce.** Es el mismo riesgo que aceptan otras carteras al teclear la passphrase en el ordenador.
- **Sin elemento seguro.** Se compensa con un diseño sin estado: no hay nada que extraer con el firmador apagado.
- **El formato con Pico y OLED montados traslada la confianza al fabricante.** Se ofrece por comodidad, con el riesgo bien explicado.
- **Cualquier formato comprado por correo vincula identidad y dirección.** Solo las piezas genéricas compradas por separado lo evitan del todo.

## 7. Pendiente

- Revisión externa de este análisis.
- Valoraciones tras probar el prototipo (alcance real del audio, consumo, ruido).
- Diseño de versiones firmadas y compilaciones reproducibles.
- Consulta normativa para los formatos montados.
- Riesgos propios de los montajes con piezas recuperadas (chips y memorias de origen desconocido), a medida que el [catálogo](es/catalogo.md) recoja montajes reales.
