from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "dist" / "tft147_previews_checked"
W, H = 320, 172
SCALE = 4
SAFE = (12, 10, 308, 162)


def font(size, bold=False):
    candidates = [
        "C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/arialbd.ttf" if bold else "C:/Windows/Fonts/arial.ttf",
    ]
    for item in candidates:
        path = Path(item)
        if path.exists():
            return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


F9 = font(9)
F10 = font(10)
F11 = font(11)
F12 = font(12)
F13 = font(13, True)
F14 = font(14, True)
F15 = font(15, True)
F16 = font(16, True)
F17 = font(17, True)
F18 = font(18, True)
F20 = font(20, True)
F22 = font(22, True)
F24 = font(24, True)
F28 = font(28, True)


BG = (4, 7, 10)
PANEL = (12, 19, 24)
PANEL2 = (16, 24, 30)
BORDER = (44, 64, 70)
MUTED = (132, 150, 154)
WHITE = (236, 246, 244)
CYAN = (44, 214, 230)
GREEN = (77, 224, 126)
YELLOW = (248, 211, 82)
ORANGE = (245, 142, 55)
RED = (240, 76, 69)


class Screen:
    def __init__(self, name):
        self.name = name
        self.img = self._new_canvas()
        self.draw = ImageDraw.Draw(self.img)
        self.text_boxes = []
        self.issues = []
        self.safe_frame()

    def _new_canvas(self):
        img = Image.new("RGB", (W, H), (0, 0, 0))
        mask = Image.new("L", (W, H), 0)
        md = ImageDraw.Draw(mask)
        md.rounded_rectangle((0, 0, W - 1, H - 1), radius=22, fill=255)
        panel = Image.new("RGB", (W, H), BG)
        pd = ImageDraw.Draw(panel)
        for y in range(H):
            pd.line((0, y, W, y), fill=(4 + y // 38, 7 + y // 28, 10 + y // 36))
        img.paste(panel, (0, 0), mask)
        return img

    def safe_frame(self):
        self.rounded((4, 4, W - 5, H - 5), outline=(40, 62, 68), radius=18)
        self.rounded((9, 9, W - 10, H - 10), outline=(22, 38, 42), radius=14)

    def rounded(self, box, fill=None, outline=BORDER, radius=7, width=1):
        self.draw.rounded_rectangle(box, fill=fill, outline=outline, radius=radius, width=width)

    def line(self, xy, fill=BORDER):
        self.draw.line(xy, fill=fill)

    def text(self, xy, value, fill=WHITE, fnt=F12, tag="text"):
        box = self.draw.textbbox(xy, value, font=fnt)
        self.draw.text(xy, value, fill=fill, font=fnt)
        self.text_boxes.append((tag, value, box))
        return box

    def text_center(self, box, value, fill=WHITE, fnt=F12, tag="text"):
        x0, y0, x1, y1 = box
        tb = self.draw.textbbox((0, 0), value, font=fnt)
        tw = tb[2] - tb[0]
        th = tb[3] - tb[1]
        xy = (x0 + (x1 - x0 - tw) / 2, y0 + (y1 - y0 - th) / 2 - 1)
        return self.text(xy, value, fill, fnt, tag)

    def pill(self, box, label, color=CYAN, fnt=F11):
        self.rounded(box, fill=(9, 18, 21), outline=(38, 61, 65), radius=7)
        self.text_center(box, label, color, fnt, "pill")

    def battery(self, x, y, pct):
        self.rounded((x, y, x + 34, y + 15), fill=(9, 16, 18), outline=WHITE, radius=3)
        self.draw.rectangle((x + 35, y + 5, x + 38, y + 10), fill=WHITE)
        fill_w = int(30 * pct / 100)
        self.draw.rounded_rectangle((x + 2, y + 2, x + 2 + fill_w, y + 13), radius=2, fill=GREEN)

    def card(self, box, title, value, color=CYAN, value_font=F20):
        x0, y0, x1, _y1 = box
        self.rounded(box, fill=PANEL, outline=BORDER, radius=7)
        self.text((x0 + 8, y0 + 5), title, MUTED, F11, "label")
        self.text((x0 + 8, y0 + 20), value, color, value_font, "value")

    def audit(self):
        sx0, sy0, sx1, sy1 = SAFE
        for tag, value, box in self.text_boxes:
            x0, y0, x1, y1 = box
            if x0 < sx0 or y0 < sy0 or x1 > sx1 or y1 > sy1:
                self.issues.append(f"{self.name}: out-of-safe {tag} '{value}' box={box}")
        for i, a in enumerate(self.text_boxes):
            for b in self.text_boxes[i + 1:]:
                if self._overlap(a[2], b[2], margin=1):
                    self.issues.append(
                        f"{self.name}: text-overlap '{a[1]}' {a[2]} with '{b[1]}' {b[2]}"
                    )
        return self.issues

    @staticmethod
    def _overlap(a, b, margin=0):
        ax0, ay0, ax1, ay1 = a
        bx0, by0, bx1, by1 = b
        return not (
            ax1 + margin <= bx0 or bx1 + margin <= ax0 or ay1 + margin <= by0 or by1 + margin <= ay0
        )


def preview_cards():
    s = Screen("01_cards")
    s.text((16, 11), "协议", MUTED, F12)
    s.pill((52, 9, 112, 25), "PD OUT", YELLOW)
    s.text((184, 11), "电量", MUTED, F12)
    s.battery(224, 9, 86)
    s.text((267, 10), "86%", GREEN, F14)
    s.line((14, 30, 306, 30), (30, 48, 52))

    cols = [14, 113, 212]
    row1 = [("输入电压", "20.1V", CYAN), ("输入电流", "3.25A", GREEN), ("功率", "65.3W", ORANGE)]
    row2 = [("电池电压", "16.4V", CYAN), ("电池电流", "-3.8A", GREEN), ("电量", "86%", YELLOW)]
    for x, item in zip(cols, row1):
        s.card((x, 36, x + 92, 76), item[0], item[1], item[2], F19 if False else F20)
    for x, item in zip(cols, row2):
        s.card((x, 84, x + 92, 124), item[0], item[1], item[2], F20)

    s.line((14, 130, 306, 130), (30, 48, 52))
    s.text((16, 134), "芯片", MUTED, F11)
    s.text((16, 147), "38.2C", GREEN, F13)
    s.text((104, 134), "NTC", MUTED, F11)
    s.text((104, 147), "31.5C", GREEN, F13)
    s.text((190, 134), "模式", MUTED, F11)
    s.pill((232, 140, 286, 157), "输出", GREEN, F12)
    return s


def preview_power_focus():
    s = Screen("02_power_focus")
    s.pill((16, 11, 72, 27), "PD输出", YELLOW, F10)
    s.pill((80, 11, 126, 27), "20V", CYAN, F11)
    s.text((220, 12), "电量", MUTED, F11)
    s.battery(258, 11, 86)

    s.rounded((86, 35, 234, 119), fill=(10, 18, 22), outline=(70, 90, 78), radius=13)
    s.text_center((86, 38, 234, 54), "实时功率", MUTED, F12)
    s.text_center((86, 55, 234, 89), "65.3W", ORANGE, F28)
    s.text_center((86, 94, 234, 113), "输入 20.1V / 3.25A", CYAN, F13)

    s.card((16, 41, 78, 82), "电池", "16.4V", CYAN, F17)
    s.card((16, 89, 78, 130), "电流", "-3.8A", GREEN, F17)
    s.card((242, 41, 304, 82), "芯片", "38.2C", GREEN, F17)
    s.card((242, 89, 304, 130), "NTC", "31.5C", GREEN, F17)
    s.line((16, 138, 304, 138), (30, 48, 52))
    s.text((18, 146), "状态", MUTED, F11)
    s.pill((56, 143, 104, 160), "输出", GREEN, F12)
    s.text((122, 145), "PD OUT", YELLOW, F14)
    s.text((190, 146), "电量 86%", WHITE, F13)
    return s


def preview_columns():
    s = Screen("03_columns")
    s.rounded((16, 16, 148, 126), fill=PANEL, outline=BORDER, radius=10)
    s.rounded((172, 16, 304, 126), fill=PANEL, outline=BORDER, radius=10)
    s.text_center((16, 20, 148, 38), "输入", CYAN, F16)
    s.text_center((172, 20, 304, 38), "电池", YELLOW, F16)
    for y, label, value, color in [(49, "电压", "20.1V", CYAN), (78, "电流", "3.25A", GREEN), (107, "功率", "65.3W", ORANGE)]:
        s.text((32, y), label, MUTED, F12)
        s.text((74, y - 5), value, color, F20)
    for y, label, value, color in [(49, "电压", "16.4V", CYAN), (78, "电流", "-3.8A", GREEN), (107, "电量", "86%", YELLOW)]:
        s.text((188, y), label, MUTED, F12)
        s.text((230, y - 5), value, color, F20)
    s.line((16, 137, 304, 137), (30, 48, 52))
    s.text((18, 146), "PD OUT", YELLOW, F13)
    s.text((86, 146), "芯片38.2C", GREEN, F13)
    s.text((188, 146), "NTC31.5C", GREEN, F13)
    s.pill((266, 143, 304, 160), "输出", GREEN, F11)
    return s


def preview_list():
    s = Screen("04_list")
    s.text((16, 11), "PD OUT", YELLOW, F14)
    s.text((88, 11), "输出正常", GREEN, F14)
    s.text((224, 11), "86%", GREEN, F16)
    s.battery(270, 11, 86)
    rows = [
        ("输入电压", "20.1V", CYAN, "输入电流", "3.25A", GREEN),
        ("输出功率", "65.3W", ORANGE, "电池电压", "16.4V", CYAN),
        ("电池电流", "-3.8A", GREEN, "芯片温度", "38.2C", GREEN),
        ("NTC温度", "31.5C", GREEN, "协议", "PD OUT", YELLOW),
    ]
    y = 37
    for left_label, left_value, left_color, right_label, right_value, right_color in rows:
        s.rounded((16, y, 304, y + 24), fill=PANEL2, outline=(30, 45, 50), radius=5)
        s.text((26, y + 5), left_label, MUTED, F12)
        s.text((92, y + 2), left_value, left_color, F17)
        s.text((166, y + 5), right_label, MUTED, F12)
        s.text((238, y + 2), right_value, right_color, F17)
        y += 29
    return s


def save_all():
    OUT.mkdir(parents=True, exist_ok=True)
    previews = [
        ("01_cards", "信息卡片版", preview_cards()),
        ("02_power_focus", "大功率居中版", preview_power_focus()),
        ("03_columns", "左右分栏版", preview_columns()),
        ("04_list", "紧凑列表版", preview_list()),
    ]

    all_issues = []
    for name, _title, screen in previews:
        issues = screen.audit()
        all_issues.extend(issues)
        native = OUT / f"{name}_320x172.png"
        big = OUT / f"{name}_1280x688.png"
        screen.img.save(native)
        screen.img.resize((W * SCALE, H * SCALE), Image.Resampling.NEAREST).save(big)

    gap = 28
    title_h = 26
    sheet_w = W * SCALE * 2 + gap * 3
    sheet_h = (H * SCALE + title_h) * 2 + gap * 3
    sheet = Image.new("RGB", (sheet_w, sheet_h), (18, 18, 18))
    sd = ImageDraw.Draw(sheet)
    for idx, (_name, title, screen) in enumerate(previews):
        x = gap + (idx % 2) * (W * SCALE + gap)
        y = gap + (idx // 2) * (H * SCALE + title_h + gap)
        sd.text((x, y), title, fill=(230, 230, 230), font=font(20, True))
        sheet.paste(screen.img.resize((W * SCALE, H * SCALE), Image.Resampling.NEAREST), (x, y + title_h))
    sheet.save(OUT / "contact_sheet_checked_4x.png")

    report = OUT / "layout_audit.txt"
    if all_issues:
        report.write_text("\n".join(all_issues) + "\n", encoding="utf-8")
    else:
        report.write_text(
            "PASS: all tracked text boxes stay inside the 12,10,308,162 safe area and do not overlap.\n",
            encoding="utf-8",
        )

    print(OUT / "contact_sheet_checked_4x.png")
    print(report)
    for issue in all_issues:
        print(issue)


if __name__ == "__main__":
    save_all()
