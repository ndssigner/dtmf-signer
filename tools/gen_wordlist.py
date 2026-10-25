#!/usr/bin/env python3
"""Generate core/ds_bip39_words.h from the official BIP-39 English word list.

Usage: python3 -I tools/gen_wordlist.py <english.txt> <out.h>

The input must be english.txt from
https://github.com/bitcoin/bips/blob/master/bip-0039/english.txt
Nothing is written unless its SHA-256 matches and it has 2048 words.
"""
import hashlib
import sys

EXPECTED_SHA256 = "2f5eed53a4727b4bf8880d8f3f199efc90e58503646d9ff8eff3a2ed3b24dbda"
COUNT = 2048
WIDTH = 8  # longest BIP-39 English word


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: gen_wordlist.py <english.txt> <out.h>\n")
        return 2
    with open(argv[1], "rb") as f:
        raw = f.read()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_SHA256:
        sys.stderr.write("sha256 mismatch: got %s\n" % digest)
        return 1
    words = raw.decode("ascii").split("\n")
    if words and words[-1] == "":
        words.pop()
    if len(words) != COUNT:
        sys.stderr.write("expected %d words, got %d\n" % (COUNT, len(words)))
        return 1
    for w in words:
        if not (3 <= len(w) <= WIDTH and w.isascii() and w.islower() and w.isalpha()):
            sys.stderr.write("unexpected word %r\n" % w)
            return 1

    out = []
    out.append("/* BIP-39 English word list, generated; do not edit by hand.\n")
    out.append(" * Source: https://github.com/bitcoin/bips/blob/master/bip-0039/english.txt\n")
    out.append(" * (sha256 %s).\n" % EXPECTED_SHA256)
    out.append(" * Regenerate: python3 -I tools/gen_wordlist.py <english.txt> core/ds_bip39_words.h\n")
    out.append(" * Layout: %d words, %d bytes each, NUL-padded (an %d-letter word has no NUL);\n"
               % (COUNT, WIDTH, WIDTH))
    out.append(" * read it byte by byte with UT_ROM_U8. */\n")
    out.append("#ifndef DS_BIP39_WORDS_H\n#define DS_BIP39_WORDS_H\n\n")
    out.append("#define DS_BIP39_WORD_COUNT %d\n#define DS_BIP39_WORD_WIDTH %d\n\n" % (COUNT, WIDTH))
    out.append("static const char DS_BIP39_WORDS[DS_BIP39_WORD_COUNT * DS_BIP39_WORD_WIDTH] UT_ROM = {\n")
    for w in words:
        padded = w.ljust(WIDTH, "\0")
        cells = ", ".join("'%s'" % c if c != "\0" else "0" for c in padded)
        out.append("    %s, /* %s */\n" % (cells, w))
    out.append("};\n\n#endif\n")
    with open(argv[2], "w", newline="\n") as f:
        f.write("".join(out))
    print("%d words, sha256 ok" % COUNT)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
