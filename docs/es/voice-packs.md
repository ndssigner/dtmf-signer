[English](../voice-packs.md) · Español

# Un paquete de voz en tu idioma

La Centralita (PEX) habla: lee en voz alta cada salida de una transacción
antes de firmar. Habla inglés, incorporado en el firmware, y español, como
paquete. **Buscamos personas que la hagan hablar en su idioma.** El
portugués de Brasil, el alemán y el francés llegarían a mucha gente, pero
cualquier idioma es bienvenido.

Un paquete no es solo una traducción. Lo que dice el firmador es lo que
compruebas antes de firmar, así que cada frase y cada número tienen que
sonar bien a quien habla el idioma. Por eso es mejor que haga el paquete, o
por lo menos lo escuche y lo apruebe, alguien nativo: tú.

Abre primero una incidencia ("A voice pack: <idioma>") para decir que te
pones con ello, y que dos personas no hagan el mismo trabajo.

## Cómo se hace un paquete

Nadie graba nada. Las frases se escriben en
[`tools/voice/phrases.json`](../../tools/voice/phrases.json), las dice una
voz libre de [Piper](https://github.com/OHF-Voice/piper1-gpl), se filtran
para que ninguna suene como una tecla de teléfono y se comprimen en un
paquete de unos 300 KB con
[`tools/voice/make_pack.py`](../../tools/voice/make_pack.py). Tres archivos
dicen todo de un idioma:

| Archivo | Qué añades |
| --- | --- |
| [`tools/voice/phrases.json`](../../tools/voice/phrases.json) | El texto de cada frase en tu idioma, y sus palabras de número |
| [`tools/voice/grammar.json`](../../tools/voice/grammar.json) | Cómo forma tu idioma los números con esas palabras |
| [`tools/voice/voices.json`](../../tools/voice/voices.json) | La voz de Piper que lo habla, fijada por su SHA-256 |

Tu idioma usa su código de dos letras (`pt`, `de`, `fr`...) en todas partes.

## 1. La voz

Elige una voz para tu idioma en
[rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices). Tiene
que:

- tener una licencia que permita redistribuir el audio con el proyecto
  (dominio público, CC0 o CC BY, como el inglés y el español), con su
  conjunto de datos conocido;
- no ser nunca la voz de una persona real que no lo haya aceptado, ni de
  nadie de este proyecto;
- sonar clara a 8 kHz, la calidad de una línea telefónica: prueba antes
  unas frases (paso 4).

Añádela a `voices.json` como las demás: su nombre, el hablante si el modelo
tiene varios, el conjunto de datos, la licencia, y la URL de cada archivo
en una revisión fija con su SHA-256. Si la voz es grave, `"eq": true`
aplica la misma corrección que la voz española (un paso alto y una
atenuación suave).

### Sin voz libre en tu idioma: la tuya

Si Piper no tiene una voz con licencia libre para tu idioma, puedes grabar
tú las frases y compartir las grabaciones con licencia libre (CC0 o CC BY).
En `voices.json`, en vez de los archivos, indica la carpeta:

```json
"pt": { "name": "recorded: <tu nombre o seudónimo>", "recorded": "tools/voice/recorded/pt", "license": "CC BY 4.0" }
```

con un WAV por frase, con su nombre (`greeting.wav`, `n_um.wav`...): 16
bits, mono, 8 kHz o más. `make_pack.py` les da el mismo tratamiento que a
una voz sintética (remuestreo, `"eq"` si lo pones, el filtro de DTMF, el
silencio recortado en los dos extremos) y lista las frases que falten.

- **Graba en un sitio silencioso,** con el mismo micrófono, la misma
  distancia y el mismo volumen en todas las frases, para que suenen como una
  sola voz en un solo día. Deja un momento de silencio antes y después de
  cada una.
- **Son unos seis minutos de voz** en unas 230 frases. Y es un compromiso:
  una frase que se añada o cambie más adelante tienes que volver a grabarla
  tú, o tu idioma se queda atrás.
- **Tu voz pasa a ser pública, para siempre,** ligada a este proyecto y
  libre para que cualquiera la reutilice. Es tu decisión; tómala a
  sabiendas. Si usas un seudónimo en otros sitios, una grabación de tu voz
  puede deshacerlo.

## 2. Las frases

Añade el texto en tu idioma a cada entrada de `phrases.json`, junto a `en` y
`es`, y a su versión `long` donde la haya (la ayuda, que se dice una vez si
el usuario duda). Mantén el sentido, el orden de las opciones y las cifras
("1, cargar la semilla." conserva su "1,"). Que sea breve: se oye por un
teléfono, y la gente actúa según lo que oye.

**Escríbelo como tiene que sonar.** Piper lee la ortografía, y se equivoca
con palabras extranjeras, siglas y términos de Bitcoin. Escríbelos como los
dice alguien de tu idioma. El paquete español hace:

| Dice | Escrito en `phrases.json` |
| --- | --- |
| DTMF signer | `de te eme efe sáiner` |
| testnet | `téstnet` |
| mainnet | `méinnet` |
| wallet | `guálet` |

Escucha cada frase (paso 4) y cambia la grafía hasta que suene bien; las
tildes, los guiones, las comas y los puntos cambian cómo lo dice Piper.

**Las palabras de número.** Cada idioma tiene las suyas: las españolas son
`n_cero`, `n_uno`..., vacías en inglés; las inglesas son `n_zero`,
`n_one`..., vacías en español. Añade las tuyas como entradas nuevas (`n_` y
la palabra, en ASCII), con texto solo en tu idioma: palabras enteras para
cada número que tu idioma dice en una sola palabra, y las piezas con las
que forma los demás.

Toda frase que no sea una palabra de número tiene que tener texto en tu
idioma: `make_pack.py` rechaza el paquete y lista las que faltan.

## 3. Los números

Los importes y las direcciones se leen en voz alta, así que los números son
lo más importante. En `grammar.json`, tu idioma dice cómo formar cualquier
número con sus palabras de número:

- `small`: la palabra de cada número de 0 a 99 que tiene la suya propia (en
  alemán, cada número hasta 99 puede ser una, `einundzwanzig` incluido);
- `scales`: cien, mil, millón..., con su singular y su plural y si se dice
  "uno" delante;
- `irregulars`: formas que cambian según lo que sigue (el español `treinta`
  solo, `treinta y` delante de una unidad).

Mira allí `es` y `en`, y `say_number()` en `make_pack.py`, que es la
referencia que sigue el firmware. Las pruebas dicen cada número de 0 a
2000, y muchos más grandes, con tu gramática.

## 4. Hazlo y escúchalo

```bash
python3 -m venv ~/.cache/dtmf-signer/venv
~/.cache/dtmf-signer/venv/bin/pip install -r tools/voice/requirements.txt
~/.cache/dtmf-signer/venv/bin/python tools/voice/make_pack.py --lang pt \
    --out-bin app/voice/packs/pt.bin --wav-dir /tmp/pt-wav
```

`--wav-dir` escribe cada frase como un WAV, tal como la reproducirá el
firmador: escúchalas todas, los números sobre todo. Luego oye el paquete en
contexto: en el simulador de la web compilado en local, o en una Pico con
[`tools/install_pex.py`](../../tools/install_pex.py).

Las palabras de número nuevas cambian la tabla de frases del firmware
(`python3 tools/voice/make_pack.py --header app/voice/ds_prompts.h --names
app/voice/ds_prompt_names.c`), así que tu paquete sale con la siguiente
versión del firmware, y los demás paquetes se vuelven a hacer con ella:
dilo en la petición de cambios y lo harán los mantenedores.

## 5. Comprueba y envía

- `python3 -m unittest discover -s tests` y las pruebas en C (`ctest`, ver
  [CONTRIBUTING](../../CONTRIBUTING.md)) tienen que pasar. Añade tu paquete
  a la prueba de talk-off, que reproduce cada frase y comprueba que ninguna
  suena como una tecla: mira cómo se incrusta `es.bin` en
  `tests/c/CMakeLists.txt` y se usa en
  `test_no_prompt_makes_a_key_at_any_alignment` (`tests/c/test_voice.c`).
- Añádelo a la web: en [`site/config.json`](../../site/config.json), dentro
  de `simulator` de la PEX, en `packs` (`"pt": "app/voice/packs/pt.bin"`),
  para que el simulador lo hable y las descargas lo ofrezcan.
- Acredita la voz en [`app/voice/VOICES.md`](../../app/voice/VOICES.md) y
  [`THIRD_PARTY.md`](../../THIRD_PARTY.md).
- En la petición de cambios, di quién ha escuchado el paquete entero y que
  los números suenan bien. Un paquete que nadie ha escuchado no se publica.
