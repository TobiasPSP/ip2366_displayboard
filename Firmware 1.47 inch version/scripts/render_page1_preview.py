from pathlib import Path
import re

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
ASCII_FONT = ROOT / "src" / "common" / "font8x16_ref.h"
ZH_FONT = ROOT / "src" / "common" / "font_zh16_ref.h"
ZH_FALLBACK = ROOT / "src" / "common" / "zh_font.c"


def rgb565_to_rgb888(color):
    r = (color >> 11) & 0x1F
    g = (color >> 5) & 0x3F
    b = color & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


COLORS = {
    "BLACK": rgb565_to_rgb888(0x0000),
    "WHITE": rgb565_to_rgb888(0xFFFF),
    "RED": rgb565_to_rgb888(0xF800),
    "GREEN": rgb565_to_rgb888(0x07E0),
    "YELLOW": rgb565_to_rgb888(0xFFE0),
    "PURPLE": rgb565_to_rgb888(0x780F),
}


def parse_ascii_font():
    text = ASCII_FONT.read_text(encoding="ascii")
    body = text[text.find("static const uint8_t font8x16_ref") :]
    rows = re.findall(r"\{([^{}]+)\}", body)
    glyphs = {}
    for idx, row in enumerate(rows[:95]):
        values = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", row)]
        if len(values) == 16:
            glyphs[chr(idx + 32)] = values
    return glyphs


def parse_zh_font_file(path, encoding="ascii"):
    text = path.read_text(encoding=encoding)
    glyphs = {}
    for cp, body in re.findall(r"\{0x([0-9A-Fa-f]+),\s*\{([^{}]+)\}\}", text):
        values = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", body)]
        if len(values) == 32:
            glyphs[chr(int(cp, 16))] = values
    return glyphs


ASCII = parse_ascii_font()
ZH = parse_zh_font_file(ZH_FALLBACK, "utf-8")
ZH.update(parse_zh_font_file(ZH_FONT, "ascii"))


def draw_char8x16(img, x, y, ch, fg, bg):
    if x >= img.width:
        return x + 8
    glyph = ASCII.get(ch, ASCII.get("?"))
    px = img.load()
    draw_y = y + 3
    for row in range(10):
        src_row = 2 + row
        line = glyph[src_row]
        for col in range(7):
            if x + col < img.width:
                px[x + col, draw_y + row] = fg if (line & (1 << col)) else bg
    return x + 8


def draw_glyph16(img, x, y, glyph, fg, bg):
    px = img.load()
    for row in range(16):
        lo = glyph[row * 2]
        hi = glyph[row * 2 + 1]
        for bit in range(8):
            px[x + bit, y + row] = fg if (lo & (1 << bit)) else bg
        for bit in range(8):
            px[x + 8 + bit, y + row] = fg if (hi & (1 << bit)) else bg
    return x + 16


def draw_text16(img, x, y, text, color_name):
    fg = COLORS[color_name]
    bg = COLORS["BLACK"]
    for ch in text:
        if ord(ch) < 0x80:
            x = draw_char8x16(img, x, y, ch, fg, bg)
        elif ch in ZH:
            x = draw_glyph16(img, x, y, ZH[ch], fg, bg)
        else:
            x += 16
    return x


def fill_rect(img, x, y, w, h, color):
    px = img.load()
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if 0 <= xx < img.width and 0 <= yy < img.height:
                px[xx, yy] = color


def draw_rect(img, x, y, w, h, color):
    fill_rect(img, x, y, w, 1, color)
    fill_rect(img, x, y + h - 1, w, 1, color)
    fill_rect(img, x, y, 1, h, color)
    fill_rect(img, x + w - 1, y, 1, h, color)


def draw_battery(img, x, y, percent):
    color = COLORS["GREEN"] if percent >= 30 else COLORS["RED"]
    fill_rect(img, x, y, 32, 16, COLORS["BLACK"])
    draw_rect(img, x, y + 3, 27, 11, COLORS["WHITE"])
    fill_rect(img, x + 27, y + 6, 3, 5, COLORS["WHITE"])
    fill_rect(img, x + 2, y + 5, percent * 23 // 100, 7, color)


def render():
    img = Image.new("RGB", (160, 80), COLORS["BLACK"])
    rows = [
        (0, "2766", "GREEN", 48, "25.0℃", "GREEN"),
        (16, "无输出", "WHITE", 88, "非快充", "GREEN"),
        (32, "电压0.00V", "YELLOW", 88, "电流0.00A", "WHITE"),
        (48, "功率0.00W", "PURPLE", 88, " 10000mWh", "GREEN"),
        (64, "端口正常", "GREEN", 0, "", "GREEN"),
    ]
    for y, left, lc, rx, right, rc in rows:
        fill_rect(img, 0, y, 160, 16, COLORS["BLACK"])
        draw_text16(img, 0, y, left, lc)
        if right:
            draw_text16(img, rx, y, right, rc)
    draw_battery(img, 96, 0, 75)
    draw_text16(img, 128, 0, " 75%", "WHITE")
    DIST.mkdir(exist_ok=True)
    out = DIST / "page1_exact_firmware_font_preview.png"
    img.save(out)
    print(out)


if __name__ == "__main__":
    render()
