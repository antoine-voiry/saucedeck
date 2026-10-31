"""LCD theme mockups (320x240, RGB565-quantized, real Adafruit GFX fonts).

Usage: python3 -I tools/lcd_mock.py <output-dir>
Writes A_candy.png, B_neon.png, C_sunset.png (2x) and sheet.png. Needs Pillow and the
Adafruit GFX library fetched by PlatformIO (.pio/libdeps/saucedeck/).
"""
import os, re, sys
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(ROOT, ".pio/libdeps/saucedeck/Adafruit GFX Library/Fonts/")
W, H = 320, 240


def load_font(name):
    src = open(FONTS + name + ".h").read()
    bm = re.search(r"Bitmaps\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", src, re.S).group(1)
    bitmap = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", bm)]
    gl = re.search(r"Glyphs\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", src, re.S).group(1)
    glyphs = [tuple(int(v) for v in g) for g in re.findall(r"\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}", gl)]
    ya = int(re.search(r"0x7E,\s*(\d+)\s*\}", src).group(1))
    return {"bitmap": bitmap, "glyphs": glyphs, "first": 0x20, "ya": ya}


F9 = load_font("FreeSans9pt7b")
FB9 = load_font("FreeSansBold9pt7b")
FB12 = load_font("FreeSansBold12pt7b")
FB18 = load_font("FreeSansBold18pt7b")


def q(rgb):  # quantize to RGB565 like the panel
    r, g, b = rgb
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    return (r5 << 3 | r5 >> 2, g6 << 2 | g6 >> 4, b5 << 3 | b5 >> 2)


def hexc(h):
    return q(tuple(int(h[i:i + 2], 16) for i in (1, 3, 5)))


def mix(a, b, t):
    return q(tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3)))


def text_w(font, s):
    return sum(font["glyphs"][ord(c) - 0x20][3] for c in s)


def text(img, font, s, x, base, col):
    px = img.load()
    col = q(col)
    for c in s:
        off, w, h, adv, xo, yo = font["glyphs"][ord(c) - 0x20]
        bit = 0
        for yy in range(h):
            for xx in range(w):
                byte = font["bitmap"][off + (bit >> 3)]
                if byte & (0x80 >> (bit & 7)):
                    X, Y = x + xo + xx, base + yo + yy
                    if 0 <= X < W and 0 <= Y < H:
                        px[X, Y] = col
                bit += 1
        x += adv


def centered(img, font, s, x0, w, base, col):
    text(img, font, s, x0 + (w - text_w(font, s)) // 2, base, col)


def wrap(font, s, maxw):
    if text_w(font, s) <= maxw:
        return [s]
    words = s.split()
    for i in range(len(words) - 1, 0, -1):
        a, b = " ".join(words[:i]), " ".join(words[i:])
        if text_w(font, a) <= maxw and text_w(font, b) <= maxw:
            return [a, b]
    return [s]


def label(img, lab, x, y, fg, maxw=68):
    for font in (FB9, F9):
        if text_w(font, lab) <= maxw:
            centered(img, font, lab, x, 74, y + 32, fg); return
    for font in (FB9, F9):
        lines = wrap(font, lab, maxw)
        if len(lines) == 2:
            centered(img, font, lines[0], x, 74, y + 23, fg); centered(img, font, lines[1], x, 74, y + 41, fg); return
    centered(img, F9, lab, x, 74, y + 32, fg)


def rrect(d, box, r, fill=None, outline=None, width=1):
    d.rounded_rectangle(box, r, fill=q(fill) if fill else None, outline=q(outline) if outline else None, width=width)


LABELS = ["Screenshot", "Chrome", "Gmail", "Telegram", "Fusion 360", "Settings",
          "Help", "YouTube", "Hide app", "iMessage", "Weather", "Bambu"]
# One hue per key (vivid, distinct neighbours)
HUES = ["#FF4D6D", "#FFB000", "#EA4335", "#2AABEE", "#FF7A00", "#8E8E93",
        "#34C759", "#FF0033", "#AF52DE", "#30D158", "#0A84FF", "#00C2A8"]
PRESSED = 6  # key 7 shown mid-press


def strip(img, d, style):
    # volume pill with green->yellow->red segments, mic pill, card line
    bg = style["strip_bg"]
    d.rectangle((0, 188, W, H), fill=q(bg))
    x0, y0, segs, sw = 8, 194, 20, 6
    vol = 13
    for i in range(segs):
        t = i / (segs - 1)
        on = mix(hexc("#30D158"), hexc("#FFD60A"), t * 2) if t < 0.5 else mix(hexc("#FFD60A"), hexc("#FF453A"), (t - 0.5) * 2)
        col = on if i < vol else style["seg_off"]
        rrect(d, (x0 + i * (sw + 2), y0, x0 + i * (sw + 2) + sw - 1, y0 + 14), 2, fill=col)
    text(img, FB12, "65%", 172, y0 + 14, (255, 255, 255))
    rrect(d, (226, y0 - 1, 312, y0 + 15), 8, fill=hexc("#30D158"))
    centered(img, FB9, "MIC 70%", 226, 86, y0 + 12, (0, 0, 0))
    # card line
    rrect(d, (8, 216, 26, 234), 5, fill=hexc("#AF52DE"))
    centered(img, FB9, "M", 8, 18, 231, (255, 255, 255))
    text(img, F9, "Daft Punk - One More Time", 34, 231, style["card_fg"])
    for i in range(3):
        d.ellipse((290 + i * 9, 222, 295 + i * 9, 227), fill=q((255, 255, 255) if i == 0 else style["seg_off"]))


def topbar(img, d, style):
    d.rectangle((0, 0, W, 21), fill=q(style["top_bg"]))
    text(img, FB12, "14:32", 6, 18, style["clock"])
    text(img, F9, "Standup 15:00", 76, 16, style["top_fg"])
    d.ellipse((290, 6, 299, 15), fill=hexc("#30D158"))
    d.ellipse((304, 6, 313, 15), fill=hexc("#0A84FF"))


def candy():
    st = {"top_bg": hexc("#14142B"), "clock": (255, 255, 255), "top_fg": hexc("#C7C7E0"),
          "strip_bg": hexc("#14142B"), "seg_off": hexc("#34344F"), "card_fg": (255, 255, 255)}
    img = Image.new("RGB", (W, H), hexc("#0B0B1A"))
    d = ImageDraw.Draw(img)
    topbar(img, d, st)
    for i, lab in enumerate(LABELS):
        c = hexc(HUES[i])
        x, y = (i % 4) * 80 + 3, 24 + (i // 4) * 54
        fill = c if i == PRESSED else mix(hexc("#0B0B1A"), c, 0.55)
        rrect(d, (x, y, x + 73, y + 49), 8, fill=fill)
        rrect(d, (x, y, x + 73, y + 4), 3, fill=c)
        fg = (0, 0, 0) if i == PRESSED else (255, 255, 255)
        label(img, lab, x, y, fg)
    strip(img, d, st)
    return img


def neon():
    st = {"top_bg": (0, 0, 0), "clock": hexc("#00E5FF"), "top_fg": hexc("#FFD60A"),
          "strip_bg": (0, 0, 0), "seg_off": hexc("#202020"), "card_fg": hexc("#FF79C6")}
    img = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(img)
    topbar(img, d, st)
    for i, lab in enumerate(LABELS):
        c = hexc(HUES[i])
        x, y = (i % 4) * 80 + 3, 24 + (i // 4) * 54
        fill = c if i == PRESSED else mix((0, 0, 0), c, 0.12)
        rrect(d, (x, y, x + 73, y + 49), 10, fill=fill, outline=c, width=2)
        fg = (0, 0, 0) if i == PRESSED else c
        label(img, lab, x, y, fg)
    strip(img, d, st)
    return img


def sunset():
    # Warm gradient rows: each tile row its own band of colour, light tiles with dark text
    st = {"top_bg": hexc("#2D1B4E"), "clock": hexc("#FFD60A"), "top_fg": (255, 255, 255),
          "strip_bg": hexc("#2D1B4E"), "seg_off": hexc("#4A3570"), "card_fg": (255, 255, 255)}
    img = Image.new("RGB", (W, H), hexc("#1A1030"))
    d = ImageDraw.Draw(img)
    topbar(img, d, st)
    rows = [["#FF6B6B", "#FF8E53", "#FFB347", "#FFD93D"],
            ["#C084FC", "#A78BFA", "#818CF8", "#60A5FA"],
            ["#34D399", "#2DD4BF", "#22D3EE", "#38BDF8"]]
    for i, lab in enumerate(LABELS):
        c = hexc(rows[i // 4][i % 4])
        x, y = (i % 4) * 80 + 3, 24 + (i // 4) * 54
        fill = (255, 255, 255) if i == PRESSED else c
        rrect(d, (x, y, x + 73, y + 49), 12, fill=fill)
        fg = hexc("#1A1030")
        label(img, lab, x, y, fg)
    strip(img, d, st)
    return img


def sheet():
    out = OUT
    imgs = [("A_candy", candy()), ("B_neon", neon()), ("C_sunset", sunset())]
    for name, im in imgs:
        im.resize((W * 2, H * 2), Image.NEAREST).save(f"{out}/{name}.png")


if len(sys.argv) != 2:
    sys.exit("usage: lcd_mock.py <output-dir>")
OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
sheet()

row1 = Image.new("RGB", (W * 3 + 40, H + 30), (40, 40, 40))
from PIL import ImageDraw as D
for i, (n, f) in enumerate([("A candy", candy), ("B neon", neon), ("C sunset", sunset)]):
    row1.paste(f(), (i * (W + 20), 30))
    D.Draw(row1).text((i * (W + 20) + 4, 8), n, fill=(255, 255, 255))
row1.resize((row1.width * 2, row1.height * 2), Image.NEAREST).save(OUT + "/sheet.png")
