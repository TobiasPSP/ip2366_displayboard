from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
W, H = 160, 80


def font(size, bold=False):
    candidates = [
        "C:/Windows/Fonts/consolab.ttf" if bold else "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/arialbd.ttf" if bold else "C:/Windows/Fonts/arial.ttf",
    ]
    for item in candidates:
        path = Path(item)
        if path.exists():
            return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


F6 = font(6)
F7 = font(7)
F8 = font(8, True)
F9 = font(9, True)
F10 = font(10, True)
F14 = font(14, True)
F18 = font(18, True)


def blend(a, b, t):
    return tuple(int(a[i] * (1 - t) + b[i] * t) for i in range(3))


def line_gradient(draw, box, c0, c1):
    x0, y0, x1, y1 = box
    span = max(1, y1 - y0)
    for y in range(y0, y1 + 1):
        t = (y - y0) / span
        draw.line((x0, y, x1, y), fill=blend(c0, c1, t))


def rounded(draw, box, outline, fill=None, radius=3, width=1):
    draw.rounded_rectangle(box, radius=radius, outline=outline, fill=fill, width=width)


def glow_text(draw, xy, text, fill, font_obj, glow=(0, 130, 255)):
    x, y = xy
    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        draw.text((x + dx, y + dy), text, fill=glow, font=font_obj)
    draw.text((x, y), text, fill=fill, font=font_obj)


def pill(draw, box, text, fill, outline, text_fill=(240, 255, 255), fnt=F7):
    rounded(draw, box, outline=outline, fill=fill, radius=3)
    tw = draw.textlength(text, font=fnt)
    x0, y0, x1, y1 = box
    draw.text((x0 + (x1 - x0 - tw) / 2, y0 + 1), text, fill=text_fill, font=fnt)


def battery(draw, x, y, percent):
    rounded(draw, (x, y, x + 23, y + 10), outline=(188, 230, 255), fill=(3, 14, 24), radius=2)
    draw.rectangle((x + 24, y + 3, x + 26, y + 7), fill=(188, 230, 255))
    fill_w = max(1, int(19 * percent / 100))
    line_gradient(draw, (x + 2, y + 2, x + 1 + fill_w, y + 8), (80, 255, 130), (255, 226, 68))


def arc_meter(draw, center, radius, pct):
    cx, cy = center
    box = (cx - radius, cy - radius, cx + radius, cy + radius)
    draw.arc(box, 130, 410, fill=(27, 52, 74), width=4)
    draw.arc(box, 130, 130 + int(280 * pct), fill=(0, 232, 255), width=4)
    draw.arc((box[0] + 3, box[1] + 3, box[2] - 3, box[3] - 3), 130, 130 + int(280 * pct), fill=(130, 255, 118), width=1)


def draw_concept():
    img = Image.new("RGB", (W, H), (1, 5, 10))
    draw = ImageDraw.Draw(img)

    line_gradient(draw, (0, 0, W - 1, H - 1), (3, 10, 23), (0, 20, 23))
    for x in range(0, W, 8):
        draw.line((x, 0, x, H), fill=(3, 22, 32))
    for y in range(0, H, 8):
        draw.line((0, y, W, y), fill=(3, 22, 32))
    draw.line((0, 15, 159, 15), fill=(0, 130, 180))
    draw.line((0, 16, 159, 16), fill=(0, 35, 55))

    glow_text(draw, (3, 2), "IP2366", (245, 255, 255), F8, (0, 96, 180))
    pill(draw, (44, 2, 72, 12), "PD OUT", (0, 57, 80), (0, 209, 255), fnt=F6)
    pill(draw, (75, 2, 98, 12), "CC", (18, 62, 22), (98, 255, 119), fnt=F6)
    battery(draw, 104, 2, 82)
    draw.text((132, 2), "82%", fill=(244, 255, 246), font=F8)

    rounded(draw, (3, 19, 72, 58), outline=(0, 188, 255), fill=(2, 13, 24), radius=4)
    draw.text((7, 21), "VSYS", fill=(108, 195, 255), font=F7)
    glow_text(draw, (7, 29), "20.0V", (246, 255, 255), F18, (0, 109, 210))
    draw.text((9, 49), "REG52  0x4E20", fill=(102, 134, 157), font=F6)

    rounded(draw, (76, 19, 119, 58), outline=(77, 255, 143), fill=(2, 17, 16), radius=4)
    draw.text((80, 21), "ISYS", fill=(135, 255, 176), font=F7)
    glow_text(draw, (80, 31), "3.25A", (235, 255, 240), F14, (0, 115, 60))
    draw.text((81, 49), "REG70", fill=(98, 150, 128), font=F6)

    arc_meter(draw, (140, 40), 18, 0.72)
    draw.text((128, 25), "PWR", fill=(255, 214, 102), font=F7)
    glow_text(draw, (126, 35), "65W", (255, 252, 220), F14, (178, 89, 0))
    draw.text((127, 51), "REG74", fill=(151, 120, 81), font=F6)

    draw.line((5, 62, 155, 62), fill=(0, 78, 112))
    pill(draw, (4, 65, 26, 76), "5V", (10, 25, 32), (55, 117, 139), (132, 158, 170), F6)
    pill(draw, (29, 65, 51, 76), "9V", (10, 25, 32), (55, 117, 139), (132, 158, 170), F6)
    pill(draw, (54, 65, 80, 76), "12V", (10, 36, 28), (96, 255, 130), (220, 255, 228), F6)
    pill(draw, (83, 65, 109, 76), "20V", (10, 36, 28), (96, 255, 130), (220, 255, 228), F6)
    draw.text((114, 64), "BAT", fill=(120, 168, 190), font=F6)
    draw.text((114, 71), "4.12V", fill=(252, 238, 99), font=F7)
    draw.text((140, 64), "NTC", fill=(120, 168, 190), font=F6)
    draw.text((140, 71), "32C", fill=(118, 255, 139), font=F7)

    draw.rectangle((0, 78, 159, 79), fill=(0, 226, 255))
    return img


def main():
    DIST.mkdir(exist_ok=True)
    img = draw_concept()
    out = DIST / "ip2366_cyber_power_screen_160x80.png"
    img.save(out)
    scale = 5
    big = img.resize((W * scale, H * scale), Image.Resampling.NEAREST)
    big_out = DIST / "ip2366_cyber_power_screen_800x400_preview.png"
    big.save(big_out)
    print(out)
    print(big_out)


if __name__ == "__main__":
    main()
