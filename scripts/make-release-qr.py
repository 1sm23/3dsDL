#!/usr/bin/env python3
"""Generate a release's FBI install QR as PNG, using the vendored encoder.

    python3 scripts/make-release-qr.py v0.8.2.1

No third-party Python packages are needed. Keep the PNG with the release
notes in docs/releases/ so its preview can be pinned to the same Git tag.
"""

import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zlib


def png_chunk(kind, data):
    return (struct.pack('>I', len(data)) + kind + data +
            struct.pack('>I', zlib.crc32(kind + data)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag', help='release tag, e.g. v0.8.2.1')
    parser.add_argument('--repo', default='1sm23/3dsDL')
    args = parser.parse_args()
    if not re.fullmatch(r'v[0-9]+(?:\.[0-9]+){2,3}', args.tag):
        parser.error('expected a release tag such as v0.8.2.1')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', args.repo):
        parser.error('expected an owner/repository name')

    root = Path(__file__).resolve().parent.parent
    url = f'https://github.com/{args.repo}/releases/download/{args.tag}/3dsDL.cia'
    with tempfile.TemporaryDirectory(prefix='3dsdl-release-qr-') as temporary:
        tool = Path(temporary) / 'qr-svg'
        svg = Path(temporary) / 'qr.svg'
        subprocess.run([
            'cc', '-O2', f'-I{root / "source/qrcodegen"}',
            str(root / 'scripts/qr-svg.c'),
            str(root / 'source/qrcodegen/qrcodegen.c'), '-o', str(tool),
        ], check=True)
        subprocess.run([str(tool), url, str(svg),
                        f'Install 3dsDL {args.tag} with FBI'], check=True)
        encoded = svg.read_text()

    # qr-svg.c writes one unit square per dark module, with a four-module
    # white quiet zone already included in its viewBox.
    modules = int(re.search(r'viewBox="0 0 (\d+) \d+"', encoded)[1])
    scale = 8
    size = modules * scale
    rows = [bytearray(b'\xff' * size) for _ in range(size)]
    for x, y in re.findall(r'M(\d+) (\d+)h1v1h-1z', encoded):
        left, top = int(x) * scale, int(y) * scale
        for row in rows[top:top + scale]:
            row[left:left + scale] = b'\0' * scale
    raw = b''.join(b'\0' + bytes(row) for row in rows)
    png = (b'\x89PNG\r\n\x1a\n' +
           png_chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 0, 0, 0, 0)) +
           png_chunk(b'IDAT', zlib.compress(raw, 9)) + png_chunk(b'IEND', b''))
    output = root / 'docs/releases' / f'{args.tag}-qr.png'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(png)
    print(f'{output}: {size}x{size}\n{url}')


if __name__ == '__main__':
    main()
