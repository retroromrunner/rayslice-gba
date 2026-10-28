#!/usr/bin/env python3
"""Render a Mode 4 framebuffer dump (qa/*.bin: page byte + 38400 fb bytes + 512 pal bytes) to PNG, scaled 3x."""
import sys
from PIL import Image

def render(path, out):
    d = open(path, "rb").read()
    assert len(d) == 1 + 240 * 160 + 512, len(d)
    fb = d[1:1 + 240 * 160]
    pal = d[1 + 240 * 160:]
    def rgb15(c):
        r = (c & 31) << 3; g = ((c >> 5) & 31) << 3; b = ((c >> 10) & 31) << 3
        return (r | r >> 5, g | g >> 5, b | b >> 5)
    pals = [rgb15(pal[i * 2] | (pal[i * 2 + 1] << 8)) for i in range(256)]
    img = Image.new("RGB", (240, 160))
    px = img.load()
    for y in range(160):
        for x in range(240):
            px[x, y] = pals[fb[y * 240 + x]]
    img = img.resize((720, 480), Image.NEAREST)
    img.save(out)
    print(out)

if __name__ == "__main__":
    render(sys.argv[1], sys.argv[2])
