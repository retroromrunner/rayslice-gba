#!/usr/bin/env python3
"""Render a slice framebuffer dump (tiles 4096 + map 2048 + pal 512) to PNG, scaled 3x.
Tiles are 4bpp, 8x8, 32 bytes each. Map is 32x32 u16: tile index + palette bank<<12.
Only the 30x20 area is drawn."""
import sys
from PIL import Image

def render(path, out):
    d = open(path, "rb").read()
    assert len(d) == 4096 + 2048 + 512, len(d)
    tiles, mp, pal = d[:4096], d[4096:4096+2048], d[4096+2048:]
    def rgb15(c):
        r = (c & 31) << 3; g = ((c >> 5) & 31) << 3; b = ((c >> 10) & 31) << 3
        return (r | r >> 5, g | g >> 5, b | b >> 5)
    pals = [rgb15(pal[i * 2] | (pal[i * 2 + 1] << 8)) for i in range(256)]
    img = Image.new("RGB", (240, 160))
    px = img.load()
    for ty in range(20):
        for tx in range(30):
            e = mp[(ty * 32 + tx) * 2] | (mp[(ty * 32 + tx) * 2 + 1] << 8)
            tile, bank = e & 0x3FF, (e >> 12) & 0xF
            base = tile * 32
            for y in range(8):
                for x in range(8):
                    byte = tiles[base + y * 4 + (x >> 1)]
                    pix = (byte >> 4) & 0xF if (x & 1) else byte & 0xF
                    px[tx * 8 + x, ty * 8 + y] = pals[bank * 16 + pix]
    img.resize((720, 480), Image.NEAREST).save(out)
    print(out)

if __name__ == "__main__":
    render(sys.argv[1], sys.argv[2])
