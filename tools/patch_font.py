"""Redraw the Xbox button icons in Font_UI_Small.fnt as DualSense symbols.

Font file format (from the game's text renderer at 0x60B0B0 and the loader at 0x60ADF0):
  +0x04 u32  texture data offset
  +0x14 u8   glyph count      +0x15 u8 first char      +0x16 u8 cell height
  +0x18      glyph records, 8 bytes each: u16 x, u16 y, u8 width, u8 advance, s8 x-offset, u8 pad
  texture    256 wide, 16-bit A4R4G4B4, height = remaining bytes / 512

Icon glyph codes (Xbox leftovers, confirmed by rendering the atlas):
  0x80 A  0x81 Y  0x82 B  0x83 X  0xA2 left trigger  0xA5 right trigger  0xA3/0xA4 sticks  0xA6 white  0xA7 black
Unused in every language file: 0x98-0x9B (30-31 px wide) -> used for the wider L1/R1/L2/R2 labels.

usage: patch_font.py <original.fnt> <patched.fnt> [preview.png]
"""
import struct, sys, zlib, math

src, dst = sys.argv[1], sys.argv[2]
preview = sys.argv[3] if len(sys.argv) > 3 else None
d = bytearray(open(src, "rb").read())
toff = struct.unpack_from("<I", d, 4)[0]
count, first, cellh = d[0x14], d[0x15], d[0x16]
W = 256
H = (len(d) - toff) // 2 // W
assert (count, first, cellh, W, H) == (222, 0x20, 16, 256, 128), "unexpected Font_UI_Small layout"

def rec_off(ch): return 0x18 + (ch - first) * 8
def get_rec(ch): return list(struct.unpack_from("<HHBBbB", d, rec_off(ch)))
def set_rec(ch, x, y, w, adv, xo=0):
    r = get_rec(ch); struct.pack_into("<HHBBbB", d, rec_off(ch), x, y, w, adv, xo, r[5])

def put(x, y, a, r, g, b):
    if 0 <= x < W and 0 <= y < H:
        struct.pack_into("<H", d, toff + (y * W + x) * 2, (a << 12) | (r << 8) | (g << 4) | b)

def clear(x0, y0, w, h):
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            put(x, y, 0, 0, 0, 0)

def blend(layers, x0, y0, w, h, ss=4):
    """layers: list of (coverage_fn(px,py)->bool, (r,g,b)) painted in order; supersampled coverage -> 4-bit alpha."""
    for y in range(h):
        for x in range(w):
            acc_a = 0.0; acc = [0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    px, py = x + (sx + 0.5) / ss, y + (sy + 0.5) / ss
                    col = None
                    for fn, c in layers:
                        if fn(px, py): col = c
                    if col is not None:
                        acc_a += 1
                        for i in range(3): acc[i] += col[i]
            if acc_a:
                a = round(acc_a / (ss * ss) * 15)
                r, g, b = (round(acc[i] / acc_a) for i in range(3))
                put(x0 + x, y0 + y, a, r, g, b)
            else:
                put(x0 + x, y0 + y, 0, 0, 0, 0)

DARK = (3, 3, 3)
BLUE, RED, PINK, GREEN = (7, 10, 15), (15, 6, 7), (14, 9, 13), (4, 13, 11)
LIGHT, INK = (13, 13, 13), (1, 1, 1)

def disc(cx, cy, rr): return lambda px, py: (px - cx) ** 2 + (py - cy) ** 2 <= rr * rr

def seg_dist(px, py, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay
    t = max(0, min(1, ((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy)))
    return math.hypot(px - (ax + t * vx), py - (ay + t * vy))

def face_button(ch, kind, color):
    x, y, w, adv, xo, _ = get_rec(ch)
    cx, cy, R = 7.0, 8.0, 7.0
    if kind == "cross":
        sym = lambda px, py: seg_dist(px, py, 4.2, 5.2, 9.8, 10.8) <= 1.05 or seg_dist(px, py, 9.8, 5.2, 4.2, 10.8) <= 1.05
    elif kind == "circle":
        sym = lambda px, py: 2.6 <= math.hypot(px - cx, py - cy) <= 4.3
    elif kind == "square":
        sym = lambda px, py: (3.7 <= px <= 10.3 and 4.7 <= py <= 11.3) and not (5.5 <= px <= 8.5 and 6.5 <= py <= 9.5)
    else:  # triangle outline
        tri = [(7.0, 4.0), (11.2, 11.3), (2.8, 11.3)]
        def inside(px, py, pts):
            s = 0
            for i in range(3):
                (ax, ay), (bx, by) = pts[i], pts[(i + 1) % 3]
                s += (bx - ax) * (py - ay) - (by - ay) * (px - ax) > 0
            return s in (0, 3)
        inner = [(7.0, 6.6), (9.1, 10.2), (4.9, 10.2)]
        sym = lambda px, py: inside(px, py, tri) and not inside(px, py, inner)
    blend([(disc(cx, cy, R), DARK), (sym, color)], x, y, 14, 16)
    set_rec(ch, x, y, 14, 16, 1)

# 5x7 bitmaps for the shoulder/trigger labels
FONT5 = {
    "L": ["1....", "1....", "1....", "1....", "1....", "1....", "11111"],
    "R": ["1111.", "1...1", "1...1", "1111.", "1.1..", "1..1.", "1...1"],
    "1": ["..1..", ".11..", "..1..", "..1..", "..1..", "..1..", ".111."],
    "2": [".111.", "1...1", "....1", "...1.", "..1..", ".1...", "11111"],
}

def label_button(ch, slot_ch, text, trigger):
    sx, sy, sw, _, _, _ = get_rec(slot_ch)       # reuse an unused wide slot's atlas space
    w = 22
    clear(sx, sy, sw, cellh)
    top, bot = (2.5, 14.5) if trigger else (3.5, 13.5)
    rad = 3.5 if trigger else 5.0
    def body(px, py):
        if not (0.5 <= px <= w - 0.5 and top <= py <= bot): return False
        cxl = min(max(px, 0.5 + rad), w - 0.5 - rad); cyl = min(max(py, top + rad), bot - rad)
        return math.hypot(px - cxl, py - cyl) <= rad
    tw = len(text) * 5 + (len(text) - 1)
    ox, oy = (w - tw) // 2, 5
    pix = set()
    for i, c in enumerate(text):
        for yy, row in enumerate(FONT5[c]):
            for xx, v in enumerate(row):
                if v == "1": pix.add((ox + i * 6 + xx, oy + yy))
    ink = lambda px, py: (int(px), int(py)) in pix
    blend([(body, LIGHT), (ink, INK)], sx, sy, w, cellh, ss=4)
    set_rec(ch, sx, sy, w, w + 1, 0)

def stick(ch, letter):
    x, y, w, adv, xo, _ = get_rec(ch)
    cx, cy = 7.5, 8.0
    pix = set()
    for yy, row in enumerate(FONT5[letter]):
        for xx, v in enumerate(row):
            if v == "1": pix.add((5 + xx, 5 + yy))
    ring = lambda px, py: 5.8 <= math.hypot(px - cx, py - cy) <= 7.4
    ink = lambda px, py: (int(px), int(py)) in pix
    blend([(disc(cx, cy, 7.4), DARK), (ring, LIGHT), (ink, LIGHT)], x, y, 15, 16)
    set_rec(ch, x, y, 15, 16, 0)

face_button(0x80, "cross", BLUE)
face_button(0x81, "triangle", GREEN)
face_button(0x82, "circle", RED)
face_button(0x83, "square", PINK)
label_button(0xA6, 0x98, "L1", trigger=False)
label_button(0xA7, 0x99, "R1", trigger=False)
label_button(0xA2, 0x9A, "L2", trigger=True)
label_button(0xA5, 0x9B, "R2", trigger=True)
stick(0xA3, "L")
stick(0xA4, "R")
open(dst, "wb").write(d)

if not preview:
    print("patched", dst)
    raise SystemExit(0)

# ---- preview: render a sample prompt with the patched font (RGB, 3x) on a dark background
def glyph_pixels(ch):
    x, y, w, adv, xo, _ = get_rec(ch)
    return x, y, w, adv, xo

sample = [b"Press \x80 to kick, \x83 to punch, \x81 to jump kick.",
          b"Hold \xa6 to block. \x82 grab, \xa5 fire, \xa7 reload.",
          b"\xa2 enter car, \xa3 click arrest, \xa4 click frisk \x8e\x8f\x90\x91"]
S = 3
lines_px = []
for line in sample:
    wsum = 0
    for c in line:
        _, _, w, adv, xo = glyph_pixels(c)
        wsum += adv if c != 0x20 else 5
    lines_px.append(wsum)
PW, PH = (max(lines_px) + 8) * S, (len(sample) * (cellh + 4) + 4) * S
img = [bytearray([30, 30, 36] * PW) for _ in range(PH)]
for li, line in enumerate(sample):
    pen = 4
    for c in line:
        gx, gy, gw, adv, xo = glyph_pixels(c)
        if c != 0x20:
            for yy in range(cellh):
                for xx in range(gw):
                    v = struct.unpack_from("<H", d, toff + ((gy + yy) * W + gx + xx) * 2)[0]
                    a = (v >> 12) & 15
                    if not a: continue
                    rgb = [((v >> s) & 15) * 17 for s in (8, 4, 0)]
                    if c < 0x80: rgb = [230, 230, 230]      # text drawn white
                    for sy in range(S):
                        row = img[(4 + li * (cellh + 4) + yy) * S + sy]
                        for sx in range(S):
                            o = ((pen + xo + xx) * S + sx) * 3
                            for i in range(3):
                                row[o + i] = (rgb[i] * a + row[o + i] * (15 - a)) // 15
        pen += adv if c != 0x20 else 5

def png_rgb(path, w, h, rows):
    raw = b"".join(b"\0" + bytes(r) for r in rows)
    def chunk(t, dd): return struct.pack(">I", len(dd)) + t + dd + struct.pack(">I", zlib.crc32(t + dd) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
png_rgb(preview, PW, PH, img)
print("patched", dst, "preview", preview)
