from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "dist" / "tft147_previews"
W, H = 320, 172
SCALE = 4


def font(size, bold=False):
    names = [
        "C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/arialbd.ttf" if bold else "C:/Windows/Fonts/arial.ttf",
    ]
    for name in names:
        path = Path(name)
        if path.exists():
            return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


F10 = font(10)
F11 = font(11)
F12 = font(12)
F13 = font(13, True)
F14 = font(14, True)
F16 = font(16, True)
F18 = font(18, True)
F20 = font(20, True)
F22 = font(22, True)
F26 = font(26, True)
F32 = font(32, True)


BG = (4, 7, 10)
PANEL = (13, 20, 25)
PANEL_2 = (17, 25, 31)
BORDER = (44, 64, 70)
MUTED = (132, 150, 154)
WHITE = (236, 246, 244)
CYAN = (44, 214, 230)
GREEN = (77, 224, 126)
YELLOW = (248, 211, 82)
ORANGE = (245, 142, 55)
RED = (240, 76, 69)
BLUE = (89, 154, 255)


def text(draw, xy, value, fill=WHITE, fnt=F12, anchor=None):
    draw.text(xy, value, fill=fill, font=fnt, anchor=anchor)


def text_center(draw, box, value, fill=WHITE, fnt=F12):
    x0, y0, x1, y1 = box
    bbox = draw.textbbox((0, 0), value, font=fnt)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    draw.text((x0 + (x1 - x0 - tw) / 2, y0 + (y1 - y0 - th) / 2 - 1), value, fill=fill, font=fnt)


def rounded(draw, box, fill=None, outline=BORDER, radius=8, width=1):
    draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)


def safe_frame(draw):
    rounded(draw, (4, 4, W - 5, H - 5), outline=(40, 62, 68), radius=18, width=1)
    rounded(draw, (9, 9, W - 10, H - 10), outline=(22, 38, 42), radius=14, width=1)


def new_canvas():
    img = Image.new("RGB", (W, H), (0, 0, 0))
    mask = Image.new("L", (W, H), 0)
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle((0, 0, W - 1, H - 1), radius=22, fill=255)
    panel = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(panel)
    for y in range(H):
        c = (4 + y // 30, 7 + y // 22, 10 + y // 34)
        d.line((0, y, W, y), fill=c)
    img.paste(panel, (0, 0), mask)
    return img


def card(draw, box, title, value, unit="", color=CYAN, sub=None):
    x0, y0, x1, y1 = box
    rounded(draw, box, fill=PANEL, outline=BORDER, radius=7)
    text(draw, (x0 + 8, y0 + 5), title, MUTED, F11)
    text(draw, (x0 + 8, y0 + 20), value, color, F22)
    if unit:
        text(draw, (x1 - 23, y0 + 28), unit, MUTED, F10)
    if sub:
        text(draw, (x0 + 8, y1 - 14), sub, MUTED, F10)


def pill(draw, box, label, color=CYAN):
    rounded(draw, box, fill=(9, 18, 21), outline=(38, 61, 65), radius=7)
    text_center(draw, box, label, color, F11)


def battery(draw, x, y, pct):
    rounded(draw, (x, y, x + 34, y + 15), fill=(9, 16, 18), outline=WHITE, radius=3)
    draw.rectangle((x + 35, y + 5, x + 38, y + 10), fill=WHITE)
    fill_w = int(30 * pct / 100)
    draw.rounded_rectangle((x + 2, y + 2, x + 2 + fill_w, y + 13), radius=2, fill=GREEN if pct >= 30 else ORANGE)


def footer(draw, chip_x=14, ntc_x=112, mode_x=222):
    text(draw, (chip_x, 143), "芯片温度", MUTED, F11)
    text(draw, (chip_x, 157), "38.2C", GREEN, F13)
    text(draw, (ntc_x, 143), "NTC温度", MUTED, F11)
    text(draw, (ntc_x, 157), "31.5C", GREEN, F13)
    text(draw, (mode_x, 143), "模式", MUTED, F11)
    text(draw, (mode_x + 34, 154), "输出", GREEN, F14)


def preview_cards():
    img = new_canvas()
    d = ImageDraw.Draw(img)
    safe_frame(d)
    text(d, (16, 10), "协议", MUTED, F12)
    pill(d, (52, 8, 112, 24), "PD OUT", YELLOW)
    text(d, (184, 10), "电量", MUTED, F12)
    battery(d, 224, 8, 86)
    text(d, (268, 9), "86%", GREEN, F14)
    for x0, title, value, color in [
        (12, "输入电压", "20.1V", CYAN),
        (112, "输入电流", "3.25A", GREEN),
        (212, "功率", "65.3W", ORANGE),
    ]:
        card(d, (x0, 34, x0 + 92, 78), title, value, color=color)
    for x0, title, value, color in [
        (12, "电池电压", "16.4V", CYAN),
        (112, "电池电流", "-3.8A", GREEN),
        (212, "电量", "86%", YELLOW),
    ]:
        card(d, (x0, 88, x0 + 92, 132), title, value, color=color)
    footer(d)
    return img


def preview_power_focus():
    img = new_canvas()
    d = ImageDraw.Draw(img)
    safe_frame(d)
    pill(d, (15, 10, 73, 27), "PD输出", YELLOW)
    pill(d, (80, 10, 126, 27), "20V", CYAN)
    battery(d, 256, 11, 86)
    text(d, (230, 11), "电量", MUTED, F11)
    rounded(d, (83, 35, 237, 123), fill=(10, 18, 22), outline=(70, 90, 78), radius=14)
    text_center(d, (83, 38, 237, 55), "实时功率", MUTED, F12)
    text_center(d, (83, 53, 237, 94), "65.3W", ORANGE, F32)
    text_center(d, (83, 94, 237, 116), "输入 20.1V / 3.25A", CYAN, F13)
    card(d, (13, 40, 78, 85), "电池", "16.4V", color=CYAN)
    card(d, (13, 92, 78, 134), "电流", "-3.8A", color=GREEN)
    card(d, (242, 40, 307, 85), "芯片", "38.2C", color=GREEN)
    card(d, (242, 92, 307, 134), "NTC", "31.5C", color=GREEN)
    text(d, (16, 148), "状态", MUTED, F11)
    pill(d, (50, 145, 100, 162), "输出", GREEN)
    text(d, (116, 148), "协议 PD OUT  电量 86%", WHITE, F13)
    return img


def preview_columns():
    img = new_canvas()
    d = ImageDraw.Draw(img)
    safe_frame(d)
    rounded(d, (13, 13, 150, 132), fill=PANEL, outline=BORDER, radius=10)
    rounded(d, (170, 13, 307, 132), fill=PANEL, outline=BORDER, radius=10)
    text_center(d, (13, 17, 150, 37), "输入", CYAN, F16)
    text_center(d, (170, 17, 307, 37), "电池", YELLOW, F16)
    text(d, (28, 48), "电压", MUTED, F12)
    text(d, (72, 43), "20.1V", CYAN, F22)
    text(d, (28, 78), "电流", MUTED, F12)
    text(d, (72, 73), "3.25A", GREEN, F22)
    text(d, (28, 108), "功率", MUTED, F12)
    text(d, (72, 103), "65.3W", ORANGE, F22)
    text(d, (185, 48), "电压", MUTED, F12)
    text(d, (229, 43), "16.4V", CYAN, F22)
    text(d, (185, 78), "电流", MUTED, F12)
    text(d, (229, 73), "-3.8A", GREEN, F22)
    text(d, (185, 108), "电量", MUTED, F12)
    text(d, (229, 103), "86%", YELLOW, F22)
    text(d, (17, 148), "PD OUT", YELLOW, F13)
    text(d, (86, 148), "芯片 38.2C", GREEN, F13)
    text(d, (188, 148), "NTC 31.5C", GREEN, F13)
    pill(d, (270, 145, 304, 162), "输出", GREEN)
    return img


def preview_list():
    img = new_canvas()
    d = ImageDraw.Draw(img)
    safe_frame(d)
    text(d, (16, 9), "PD OUT", YELLOW, F14)
    text(d, (88, 9), "输出正常", GREEN, F14)
    text(d, (225, 9), "86%", GREEN, F16)
    battery(d, 270, 10, 86)
    rows = [
        ("输入电压", "20.1V", CYAN, "输入电流", "3.25A", GREEN),
        ("输出功率", "65.3W", ORANGE, "电池电压", "16.4V", CYAN),
        ("电池电流", "-3.8A", GREEN, "芯片温度", "38.2C", GREEN),
        ("NTC温度", "31.5C", GREEN, "协议", "PD OUT", YELLOW),
    ]
    y = 36
    for left_label, left_value, left_color, right_label, right_value, right_color in rows:
        rounded(d, (14, y, 306, y + 25), fill=PANEL_2, outline=(30, 45, 50), radius=5)
        text(d, (24, y + 5), left_label, MUTED, F12)
        text(d, (91, y + 2), left_value, left_color, F18)
        text(d, (170, y + 5), right_label, MUTED, F12)
        text(d, (239, y + 2), right_value, right_color, F18)
        y += 30
    return img


def save_all():
    OUT.mkdir(parents=True, exist_ok=True)
    previews = [
        ("01_cards", "信息卡片版", preview_cards()),
        ("02_power_focus", "大功率居中版", preview_power_focus()),
        ("03_columns", "左右分栏版", preview_columns()),
        ("04_list", "紧凑列表版", preview_list()),
    ]
    big_paths = []
    for name, _title, img in previews:
        native = OUT / f"{name}_320x172.png"
        big = OUT / f"{name}_1280x688.png"
        img.save(native)
        img.resize((W * SCALE, H * SCALE), Image.Resampling.NEAREST).save(big)
        big_paths.append(big)

    gap = 28
    sheet_w = W * SCALE * 2 + gap * 3
    sheet_h = H * SCALE * 2 + gap * 3 + 24
    sheet = Image.new("RGB", (sheet_w, sheet_h), (18, 18, 18))
    sd = ImageDraw.Draw(sheet)
    for idx, (_name, title, img) in enumerate(previews):
        x = gap + (idx % 2) * (W * SCALE + gap)
        y = gap + (idx // 2) * (H * SCALE + gap + 24)
        sd.text((x, y), title, fill=(230, 230, 230), font=font(20, True))
        sheet.paste(img.resize((W * SCALE, H * SCALE), Image.Resampling.NEAREST), (x, y + 24))
    sheet_path = OUT / "contact_sheet_4x.png"
    sheet.save(sheet_path)

    print(sheet_path)
    for path in big_paths:
        print(path)


if __name__ == "__main__":
    save_all()
