#!/usr/bin/env python3
"""Writes the ZIP archives used by the extraction tests into a folder."""

import os
import struct
import sys
import zipfile

BINARY = bytes(range(256)) * 40
README = b"hello from a zip\n"


def write(path, entries, compression=zipfile.ZIP_DEFLATED, zip64=False):
    with zipfile.ZipFile(path, "w", compression=compression) as archive:
        for name, data, *options in entries:
            if name.endswith("/"):
                archive.writestr(zipfile.ZipInfo(name), b"")
                continue
            info = zipfile.ZipInfo(name)
            info.compress_type = options[0] if options else compression
            with archive.open(info, "w", force_zip64=zip64) as handle:
                handle.write(data)


def main(folder):
    os.makedirs(folder, exist_ok=True)
    join = lambda name: os.path.join(folder, name)

    write(join("basic.zip"), [
        ("readme.txt", README),
        ("3ds/App/", b""),
        ("3ds/App/App.3dsx", BINARY, zipfile.ZIP_STORED),
        ("empty.txt", b""),
    ])

    write(join("evil.zip"), [
        ("../escape.txt", b"x"),
        ("/abs.txt", b"x"),
        ("C:/drive.txt", b"x"),
        ("ok/../../up.txt", b"x"),
        ("dir\\win.txt", b"windows separators\n"),
        ("good.txt", b"good\n"),
    ])

    write(join("zip64.zip"), [("big.bin", BINARY)], zip64=True)

    write(join("mixed.zip"), [
        ("bz.txt", b"bzip2 is not supported\n", zipfile.ZIP_BZIP2),
        ("plain.txt", b"plain\n"),
    ])

    write(join("utf8.zip"), [("na\u00efve.txt", b"utf8\n")])

    # Flip one byte of stored data so the CRC check must fail.
    write(join("corrupt.zip"), [("data.bin", b"A" * 1000)],
          compression=zipfile.ZIP_STORED)
    with open(join("corrupt.zip"), "r+b") as handle:
        header = handle.read(30)
        name_length, extra_length = struct.unpack("<HH", header[26:30])
        handle.seek(30 + name_length + extra_length + 10)
        handle.write(b"B")

    with open(join("basic.zip"), "rb") as handle:
        data = handle.read()
    with open(join("truncated.zip"), "wb") as handle:
        handle.write(data[: len(data) // 2])


if __name__ == "__main__":
    main(sys.argv[1])
