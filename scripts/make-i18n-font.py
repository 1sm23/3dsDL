#!/usr/bin/env python3
"""Build the offline A4 BCFNT fallback from a pinned OFL font (see resources/fonts)."""
import argparse
import hashlib
import json
import math
import pathlib
import re
import struct

from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont

ROOT = pathlib.Path(__file__).resolve().parents[1]
SIZE = 24
CELL = 32
BASELINE = 25
SHEET = 512
PER_ROW = SHEET // (CELL + 1)
PER_SHEET = PER_ROW ** 2


def pack(fmt, *values):
    return struct.pack('<' + fmt, *values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('font', type=pathlib.Path, help='NotoSansCJKtc-Regular.otf')
    args = parser.parse_args()
    manifest = json.loads((ROOT / 'resources/fonts/fallback.json').read_text())
    if hashlib.sha256(args.font.read_bytes()).hexdigest() != manifest['source_sha256']:
        parser.error('Font source does not match the pinned SHA-256')
    catalog = json.loads((ROOT / 'romfs/i18n.json').read_text(encoding='utf-8'))
    registry = (ROOT / 'source/core/i18n.cpp').read_text(encoding='utf-8')
    names = re.findall(r'\{"[^"\n]+", "([^"\n]+)"\}', registry)
    strings = [text for values in catalog.values() for text in values.values()] + names
    codes = sorted({ord(ch) for text in strings for ch in text if ord(ch) > 127} | {ord('?')})
    cmap = TTFont(args.font).getBestCmap()
    missing = [f'U+{code:04X}' for code in codes if code not in cmap and code != 0x200b]
    if missing:
        parser.error('Source font missing: ' + ', '.join(missing))
    font = ImageFont.truetype(str(args.font), SIZE)
    sheets = [Image.new('L', (SHEET, SHEET)) for _ in range(math.ceil(len(codes) / PER_SHEET))]
    widths = bytearray()
    for index, code in enumerate(codes):
        if code == 0x200b:  # Unicode zero-width space has no ink or advance.
            widths += pack('bBB', 0, 0, 0)
            continue
        char = chr(code)
        left, top, right, bottom = font.getbbox(char, anchor='ls')
        if right - left > CELL or bottom - top > CELL or top + BASELINE < 0 or bottom + BASELINE > CELL:
            parser.error(f'U+{code:04X} does not fit its cell')
        sheet = sheets[index // PER_SHEET]
        slot = index % PER_SHEET
        x = slot % PER_ROW * (CELL + 1) + 1
        y = slot // PER_ROW * (CELL + 1) + 1
        ImageDraw.Draw(sheet).text((x - left, y + BASELINE), char, font=font, fill=255, anchor='ls')
        widths += pack('bBB', left, right - left, round(font.getlength(char)))

    # PICA textures store 8x8 tiles in Morton order, with the first alpha
    # sample in the low nibble. BCFNT reserves one pixel before each cell.
    texture = bytearray()
    for sheet in sheets:
        pixels = sheet.load()
        for tile_y in range(0, SHEET, 8):
            for tile_x in range(0, SHEET, 8):
                samples = []
                for morton in range(64):
                    x = sum(((morton >> (2 * bit)) & 1) << bit for bit in range(3))
                    y = sum(((morton >> (2 * bit + 1)) & 1) << bit for bit in range(3))
                    samples.append((pixels[tile_x + x, tile_y + y] + 8) // 17)
                texture += bytes(samples[i] | samples[i + 1] << 4 for i in range(0, 64, 2))

    # All offsets point past each block's eight-byte magic/size header.
    tglp = 0x34
    sheet_offset = 0x80  # Texture data must be 0x80 aligned for PICA.
    cwdh = sheet_offset + len(texture)
    width_data = widths + bytes((-len(widths)) % 4)
    cmap_offset = cwdh + 16 + len(width_data)
    # A dense table avoids a linear scan of the subset on every rendered glyph.
    indices = {code: index for index, code in enumerate(codes)}
    mapping = b''.join(pack('H', indices.get(code, 0xffff)) for code in range(codes[0], codes[-1] + 1))
    mapping += bytes((-len(mapping)) % 4)
    file_size = cmap_offset + 20 + len(mapping)
    data = pack('4sHHIII', b'CFNT', 0xfeff, 20, 0x03000000, file_size, 4)
    data += pack('4sIBBHbBBBIIIBBBB', b'FINF', 32, 1, 32, codes.index(ord('?')),
                 0, SIZE, SIZE, 1, tglp + 8, cwdh + 8, cmap_offset + 8, CELL, CELL, BASELINE, 0)
    data += pack('4sIBBBB I HHHHHH I', b'TGLP', 32, CELL, CELL, BASELINE, CELL,
                 SHEET * SHEET // 2, len(sheets), 11, PER_ROW, PER_ROW, SHEET, SHEET, sheet_offset)
    data += bytes(sheet_offset - len(data)) + texture
    data += pack('4sIHHI', b'CWDH', 16 + len(width_data), 0, len(codes) - 1, 0) + width_data
    data += pack('4sIHHHHI', b'CMAP', 20 + len(mapping), codes[0], codes[-1], 1, 0, 0) + mapping
    assert len(data) == file_size
    output = ROOT / 'romfs/ui/fallback.bcfnt'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    print(f'{output}: {len(codes)} glyphs, {len(sheets)} sheets, {len(data)} bytes')


if __name__ == '__main__':
    main()
