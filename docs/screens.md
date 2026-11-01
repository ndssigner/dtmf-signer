# The screens

How the Pico build looks and how you move through it, on its 128 × 64 OLED
with five buttons. The pictures are not photos of the board: they come from
the firmware's own code run on a computer, pixel for pixel, with test data
only (the seed 00…00, PIN `TEST`, passphrase `Ab 1!`). To make them again
after a change:

```
cmake --build build --target screens
python3 tools/render_screens.py build/tests/c/screens docs/images/screens
```

## The buttons

| Button | What it does |
| :--- | :--- |
| ▲ ▼ | Move through menus and pages |
| ◀ ▶ | Move in the picker; ◀ goes back on most screens |
| OK | Choose, continue, add a character |
| OK held (1.5 s) | Finish the PIN or the passphrase; sign, on the last page of the review |
| ◀ held (3 s) | **Emergency wipe**, on any screen |

The screen is turned 180 degrees by default, as the breadboard mounts it.

## Loading the seed

### The home menu

![Home menu](images/screens/home.png)

▲ ▼ move the highlight and OK chooses. `Load seed` starts listening. The
three settings change with OK: the network (mainnet or testnet), the QR
codes' contrast and their size (2 px per module, or 1 px for bigger codes).

### Listening

![Listening, waiting](images/screens/seed-waiting.png)
![Listening, receiving](images/screens/seed-receiving.png)

Play the seed's tones into the audio cable. The second row says what comes
in: `waiting` (nothing), `* receiving` (keys are being heard), and the keys
count up. A seed's keys are counted, never shown. `lost` counts audio the
board could not keep up with. ◀ goes back home.

![Too quiet to read](images/screens/too-quiet.png)
![Not a seed](images/screens/not-a-seed.png)

`! too quiet to read` means sound is arriving but no key can be read: turn
the volume up. `Not a seed` means a frame of something else arrived (a
PSBT, for example); listening goes on. When a frame is lost with its tones
too quiet, `Too quiet: volume up` is added below.

### The PIN

![PIN, empty](images/screens/pin-empty.png)
![PIN, typing](images/screens/pin-typing.png)

The seed arrives encrypted with a PIN. The arrows move over the letters and
digits, wrapping around the edges; OK adds the character and the cursor
stays, so a repeated letter is OK, OK. The box on the right shows the
chosen character three times larger. The arrow cell at the end deletes the
last character; on an empty PIN it goes back. Hold OK to finish (with
nothing typed: no PIN). The PIN is shown in the clear: only the person
holding the signer sees it.

![Working](images/screens/working.png)

`Working...` comes up while the seed is decrypted and the wallet derived, a
few seconds on the RP2040.

### Fingerprints and passphrase

![Seed fingerprint](images/screens/seed-fingerprint.png)

The seed's fingerprint: check it against the one the sender shows. A wrong
PIN gives an error or a different fingerprint. OK continues, ◀ discards the
seed.

![Passphrase?](images/screens/passphrase-ask.png)

OK for no passphrase, ▶ to type one.

![Passphrase, letters](images/screens/passphrase-letters.png)
![Passphrase, digits and symbols](images/screens/passphrase-digits.png)

The passphrase picker works like the PIN's, over four pages of printable
ASCII: `abc`, `ABC`, `123` (digits and the commonest symbols) and `<{~`
(the rest). Down from the last row goes to the next page, up from the first
row to the previous one; the tabs at the bottom show where you are. Space,
`.` and `-` are on both letter pages, and the delete cell is in the same
corner of every page. Only ASCII, on purpose: see the passphrase notes in
[the hardware specification](hardware-minimo.md).

![Wallet fingerprint](images/screens/wallet-fingerprint.png)

The final wallet's fingerprint, with the passphrase. A passphrase with one
letter wrong gives another valid, empty wallet: this number is the check.

## The wallet

![Ready](images/screens/ready.png)

The wallet is loaded. `Sign PSBT` starts listening for a PSBT, `Show XPUB`
shows the account as an animated QR code, `Wipe` clears everything (as the
emergency wipe does).

![XPUB](images/screens/xpub.png)

The xpub for the watch-only wallet (Sparrow, for example). The right column
gives the number of parts, which one is on the screen (counting 1 to N and
starting over) and, after the first pass, `looping`. ◀ goes back. The QR
code uses its own contrast setting.

## Signing

### Receiving the PSBT

![Sign, waiting](images/screens/sign-waiting.png)
![Sign, receiving](images/screens/sign-receiving.png)

As for the seed, plus the last keys heard (a PSBT is public) and the parts
received out of those needed. Missing parts are no problem: the fountain
code takes any of them.

### The review

The review is a row of cards, ▲ ▼ to move, ◀ to cancel; each card says
where you are at the top right.

![Review, an output sent](images/screens/review-send.png)
![Review, the change](images/screens/review-change.png)

One card per output. The top says what it is: `Send to`, or, inverted,
`Change, yours` or `To this wallet` (only for outputs the signer has proved
belong to this wallet). Then the amount with all 8 decimals, twice as tall,
and the whole address in groups of 4, never split across cards: compare it
group by group with the wallet on the computer. In addresses `g`, `j`, `p`,
`q` and `y` go below the line, so `g` and `q` do not look like `9`.

![Review, the fee](images/screens/review-fee.png)

The fee, then what is sent, the change and the total (sent plus fee). A
card `Check` follows when there is something to warn about: a high fee, or
inputs that will not be signed.

![Review, the confirmation](images/screens/review-confirm.png)

The last card: what goes out, fee included, and `Hold OK: sign`. Signing
is offered only here, once every card has been seen; a tap that came before
the review was on the screen does not count, and the bar then says
`Tap OK, then hold OK`.

![Rejected](images/screens/rejected.png)

A PSBT the signer will not sign gives `Rejected` and the reason, here a
change output that claims this wallet's path with a foreign key
(`DS_ERR_CHANGE`). Nothing is signed; OK goes back.

### The signed PSBT

![Signed](images/screens/signed.png)

The signed PSBT as an animated QR code, read by the wallet on the computer.
◀ when it has been read.

## The emergency wipe

![Wiped](images/screens/wiped.png)

Hold ◀ for 3 seconds on any screen: the seed, the PIN, the passphrase and
everything derived are cleared, and the board restarts.
