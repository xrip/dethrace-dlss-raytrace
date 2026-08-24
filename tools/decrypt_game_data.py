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

This file contains the full cipher and has no project-local dependencies. It can
be copied next to any Carmageddon data pack and run with Python 3. Comment-only
lines and inline comments are decoded too; the cipher changes key after a
decoded ``//`` marker.

Every file is verified by re-encrypting the decrypted result and comparing it
byte-for-byte with the original. A file is only written if that check passes.
"""

import argparse
import enum
import pathlib


LONG_KEY = (
    0x6C, 0x1B, 0x99, 0x5F, 0xB9, 0xCD, 0x5F, 0x13,
    0xCB, 0x04, 0x20, 0x0E, 0x5E, 0x1C, 0xA1, 0x0E,
)
OTHER_LONG_KEY = (
    0x67, 0xA8, 0xD6, 0x26, 0xB6, 0xDD, 0x45, 0x1B,
    0x32, 0x7E, 0x22, 0x13, 0x15, 0xC2, 0x94, 0x37,
)
DEMO_KEY = (
    0x58, 0x50, 0x3A, 0x76, 0xCB, 0xB6, 0x85, 0x65,
    0x15, 0xCD, 0x5B, 0x07, 0xB1, 0x68, 0xDE, 0x3A,
)


class Method(enum.Enum):
    Method1 = "1"
    Method2 = "2"
    Demo = "demo"


class Codec:
    def __init__(self, method):
        self.method = method

    def _crypt_line(self, line, decoding):
        line = line.rstrip(b"\r\n")
        key = DEMO_KEY if self.method == Method.Demo else LONG_KEY
        seed = len(line) % len(key)
        result = bytearray()

        for i, value in enumerate(line):
            if self.method != Method.Demo:
                prior = result if decoding else line
                if prior[max(0, i - 2):i] == b"//":
                    key = OTHER_LONG_KEY

            if self.method == Method.Method2:
                if value == ord("\t"):
                    value = 0x80
                value = (value - 0x20) & 0xFF
                if value & 0x80 == 0:
                    value ^= key[seed] & 0x7F
                value = (value + 0x20) & 0xFF
                if value == 0x80:
                    value = ord("\t")
            else:
                if value == ord("\t"):
                    value = 0x9F
                value = (((value - 0x20) ^ key[seed]) & 0x7F) + 0x20
                if value == 0x9F:
                    value = ord("\t")
                if not decoding and self.method == Method.Demo \
                        and value in (ord("\n"), ord("\r")):
                    value |= 0x80

            result.append(value)
            seed = (seed + 7) % len(key)

        return bytes(result)

    def decode_line(self, line):
        return self._crypt_line(line, True)

    def encode_line(self, line):
        return self._crypt_line(line, False)


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
    decoded = Codec(Method.Method1).decode_line(head[1:].rstrip(b"\r\n"))
    return Method.Method1 if decoded[:6] == b"0.01\t\t" else Method.Method2


def plaintext_score(data):
    """Count bytes which are not normal Carmageddon text."""
    return sum(
        value not in (ord("\t"), ord("\r"), ord("\n"))
        and not 0x20 <= value <= 0x7E
        for value in data
    )


def select_method(data, default_method):
    """Use the pack method unless another retail method is clearly better."""
    if default_method == Method.Demo:
        return default_method

    other_method = (
        Method.Method1
        if default_method == Method.Method2
        else Method.Method2
    )
    candidates = []
    for method in (default_method, other_method):
        plain = decrypt(data, Codec(method))
        candidates.append((plaintext_score(plain), method != default_method, method))
    return min(candidates, key=lambda candidate: candidate[:2])[2]


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

    forced_method = Method(args.method) if args.method else None
    default_method = forced_method or detect_method(root)
    suffix = " (forced)" if forced_method else " (per-file fallback enabled)"
    print(f"cipher method: {default_method.value}{suffix}")

    encrypted = skipped = written = 0
    failures = []
    method_counts = {method: 0 for method in Method}

    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.upper() != ".TXT":
            continue
        data = path.read_bytes()
        if data[:1] != b"@":
            skipped += 1
            continue
        encrypted += 1

        method = forced_method or select_method(data, default_method)
        method_counts[method] += 1
        codec = Codec(method)
        plain = decrypt(data, codec)
        if reencrypt(plain, data, codec) != data:
            failures.append(path.relative_to(root))
            continue

        if args.write:
            path.write_bytes(plain)
            written += 1

    print(f"encrypted .TXT found : {encrypted}")
    print(f"plaintext .TXT skipped: {skipped}")
    used = ", ".join(
        f"{method.value}={count}"
        for method, count in method_counts.items()
        if count
    )
    print(f"files by cipher method: {used}")
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
