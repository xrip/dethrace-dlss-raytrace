#!/usr/bin/env python3
"""Decrypt Carmageddon's encrypted .TXT game data in place.

Only .TXT files are encrypted; every other asset (.PIX, .DAT, .ACT, .MAT, .FLI,
.WAV, .TAB, .SMK) is already stored in a plain engine-native or standard format.

Encryption is per line: a line beginning with '@' has the remainder scrambled by
a 16-byte XOR cipher (see EncodeLine() in src/DETHRACE/common/utility.c). Lines
without the '@' prefix are plaintext and are read as-is by
GetALineWithNoPossibleService() (src/DETHRACE/common/utility.c:262).

That is what makes decrypting safe: the game's line reader already handles
plaintext lines. The only thing rejecting a decrypted file is the first-byte
'@' check in OldDRfopen() (src/DETHRACE/common/loading.c:3472).

The cipher itself is reused from tools/decode_datatxt.py -- not reimplemented.

Every file is verified by re-encrypting the decrypted result and comparing it
byte-for-byte with the original. A file is only written if that check passes.
"""

import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from decode_datatxt import CODECS, Method  # noqa: E402


def split_lines(data):
    """Split into (body, eol) pairs, preserving the original line endings."""
    lines = []
    start = 0
    for i in range(len(data) + 1):
        if i == len(data) or data[i] == 0x0A:
            if i == len(data) and start >= len(data):
                break
            line = data[start:min(i + 1, len(data))]
            end = len(line)
            while end > 0 and line[end - 1] in (0x0A, 0x0D):
                end -= 1
            lines.append((line[:end], line[end:]))
            start = i + 1
    return lines


def decrypt(data, codec):
    """Decrypt '@' lines, pass everything else through untouched."""
    out = bytearray()
    for body, eol in split_lines(data):
        if body[:1] == b"@":
            out += codec.decode_line(body[1:]) + eol
        else:
            out += body + eol
    return bytes(out)


def reencrypt(plain, original, codec):
    """Re-encrypt, restoring each line's original '@' flag, for verification."""
    flags = [body[:1] == b"@" for body, _ in split_lines(original)]
    out = bytearray()
    for i, (body, eol) in enumerate(split_lines(plain)):
        if i < len(flags) and flags[i]:
            out += b"@" + codec.encode_line(body) + eol
        else:
            out += body + eol
    return bytes(out)


def detect_method(root):
    """Mirror the probe the game does in EncodeLine() (utility.c:111-134)."""
    general = root / "DATA" / "GENERAL.TXT"
    if not general.is_file():
        return Method.Method2
    head = general.read_bytes().split(b"\n", 1)[0]
    if head[:1] != b"@":
        return Method.Method2
    decoded = CODECS[Method.Method1]().decode_line(head[1:].rstrip(b"\r\n"))
    return Method.Method1 if decoded[:6] == b"0.01\t\t" else Method.Method2


def main():
    ap = argparse.ArgumentParser(
        allow_abbrev=False,
        description="Decrypt Carmageddon .TXT game data in place")
    ap.add_argument("root", metavar="GAMEDIR",
                    help="game directory (the one containing DATA/)")
    ap.add_argument("--write", action="store_true",
                    help="actually modify files (default: dry run)")
    ap.add_argument("--method", choices=[e.value for e in Method],
                    help="force cipher method (default: autodetect)")
    args = ap.parse_args()

    root = pathlib.Path(args.root)
    if not (root / "DATA").is_dir():
        ap.error(f"{root} does not contain a DATA/ directory")

    method = Method(args.method) if args.method else detect_method(root)
    codec = CODECS[method]()
    print(f"cipher method: {method.value}")

    encrypted = skipped = written = 0
    failures = []

    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.upper() != ".TXT":
            continue
        data = path.read_bytes()
        if data[:1] != b"@":
            skipped += 1
            continue
        encrypted += 1

        plain = decrypt(data, codec)
        if reencrypt(plain, data, codec) != data:
            failures.append(path.relative_to(root))
            continue

        if args.write:
            path.write_bytes(plain)
            written += 1

    print(f"encrypted .TXT found : {encrypted}")
    print(f"plaintext .TXT skipped: {skipped}")
    print(f"verification failures : {len(failures)}")
    for f in failures[:10]:
        print(f"  FAILED {f}")

    if args.write:
        print(f"decrypted in place    : {written}")
    else:
        print("dry run - nothing written (pass --write to apply)")

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
