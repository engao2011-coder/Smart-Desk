#!/usr/bin/env python3
"""Generate Adafruit-GFX font headers (TFT_eSPI compatible) from a TTF.

Replicates Adafruit fontconvert: 141 DPI, FT_LOAD_TARGET_MONO, glyphs
byte-aligned, MSB-first, yOffset = 1 - bitmap_top, xAdvance = advance>>6.
Char range 0x20..0x7E (matches the stock FreeFonts so it is a true drop-in).
"""
import sys, freetype

DPI = 141
FIRST, LAST = 0x20, 0x7E

def gen(ttf, weight, pt, name):
    face = freetype.Face(ttf)
    try:
        face.set_var_design_coords([float(weight)])
    except Exception as e:
        print(f"  (variable-axis set failed: {e})", file=sys.stderr)
    face.set_char_size(height=pt * 64, hres=DPI, vres=DPI)

    bitmaps = []      # flat list of output bytes
    glyphs = []       # (offset, w, h, xadv, xoff, yoff, code)
    for code in range(FIRST, LAST + 1):
        face.load_char(chr(code), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
        g = face.glyph
        bm = g.bitmap
        w, h, pitch = bm.width, bm.rows, bm.pitch
        offset = len(bitmaps)
        # pack continuously MSB-first, then pad to byte boundary
        acc, nbits = 0, 0
        out = []
        for y in range(h):
            for x in range(w):
                byte = bm.buffer[y * pitch + (x >> 3)]
                bit = (byte >> (7 - (x & 7))) & 1
                acc = (acc << 1) | bit
                nbits += 1
                if nbits == 8:
                    out.append(acc); acc, nbits = 0, 0
        if nbits:
            out.append(acc << (8 - nbits))
        bitmaps.extend(out)
        glyphs.append((offset, w, h, g.advance.x >> 6,
                       g.bitmap_left, 1 - g.bitmap_top, code))

    yadv = face.size.height >> 6
    return emit(name, bitmaps, glyphs, yadv)

def emit(name, bitmaps, glyphs, yadv):
    L = []
    L.append(f"// {name} — generated from Montserrat (Adafruit GFX format, 141 DPI)")
    L.append(f"// Char range 0x20-0x7E. Drop-in replacement for FreeSansBold*pt7b.")
    L.append(f"#pragma once")
    L.append(f"const uint8_t {name}Bitmaps[] PROGMEM = {{")
    for i in range(0, len(bitmaps), 12):
        L.append("  " + " ".join(f"0x{b:02X}," for b in bitmaps[i:i+12]))
    L.append("};")
    L.append(f"const GFXglyph {name}Glyphs[] PROGMEM = {{")
    for (off, w, h, xa, xo, yo, code) in glyphs:
        ch = chr(code) if code != 0x5C else "backslash"
        L.append(f"  {{ {off:5d}, {w:3d}, {h:3d}, {xa:3d}, {xo:4d}, {yo:4d} }},  // 0x{code:02X} '{ch}'")
    L.append("};")
    L.append(f"const GFXfont {name} PROGMEM = {{")
    L.append(f"  (uint8_t  *){name}Bitmaps,")
    L.append(f"  (GFXglyph *){name}Glyphs,")
    L.append(f"  0x{FIRST:02X}, 0x{LAST:02X}, {yadv} }};")
    L.append("")
    return "\n".join(L)

if __name__ == "__main__":
    ttf, outdir = sys.argv[1], sys.argv[2]
    sizes = [(9, "MontserratBold9pt7b"), (12, "MontserratBold12pt7b"),
             (18, "MontserratBold18pt7b"), (24, "MontserratBold24pt7b")]
    for pt, nm in sizes:
        txt = gen(ttf, 700, pt, nm)
        with open(f"{outdir}/{nm}.h", "w") as f:
            f.write(txt)
        nbytes = txt.count("0x")
        print(f"wrote {nm}.h  (~{nbytes} bitmap bytes)")
