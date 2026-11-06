from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "dist" / "tft147_firmware_pages"
W, H = 320, 172
SCALE = 4
SAFE = (12, 10, 308, 162)


def font(size, bold=False):
    for name in (
        "C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/arialbd.ttf" if bold else "C:/Windows/Fonts/arial.ttf",
    ):
        path = Path(name)
        if path.exists():
            return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


F10 = font(10)
F11 = font(11)
F12 = font(12)
F14 = font(14, True)
F16 = font(16, True)
F18 = font(18, True)
F22 = font(22, True)
F28 = font(28, True)

BG = (4, 7, 10)
PANEL = (12, 19, 24)
BORDER = (44, 64, 70)
MUTED = (132, 150, 154)
WHITE = (236, 246, 244)
CYAN = (44, 214, 230)
GREEN = (77, 224, 126)
YELLOW = (248, 211, 82)
ORANGE = (245, 142, 55)


def ref_w(text):
    return sum(8 if ord(ch) < 128 else 16 for ch in text)


def ascii_w(text, scale):
    return len(text) * 6 * scale


class Screen:
    def __init__(self, name):
        self.name = name
        self.img = Image.new("RGB", (W, H), BG)
        self.d = ImageDraw.Draw(self.img)
        self.boxes = []
        self.issues = []
        self.safe_frame()

    def safe_frame(self):
        self.d.rounded_rectangle((4, 4, W - 5, H - 5), radius=18, outline=(40, 62, 68))
        self.d.rounded_rectangle((9, 9, W - 10, H - 10), radius=14, outline=(22, 38, 42))

    def round(self, box, fill=None):
        self.d.rounded_rectangle(box, radius=7, fill=fill, outline=BORDER)

    def hline(self, y):
        self.d.line((14, y, 306, y), fill=BORDER)

    def ref(self, x, y, text, color=MUTED):
        self.d.text((x, y - 2), text, fill=color, font=F16 if any(ord(c) > 127 for c in text) else F14)
        self.boxes.append((text, (x, y, x + ref_w(text), y + 16)))

    def ascii(self, x, y, text, color=WHITE, scale=1):
        fnt = {1: F12, 2: F22, 3: F28}.get(scale, F12)
        self.d.text((x, y - 4 if scale > 1 else y - 2), text.rstrip(), fill=color, font=fnt)
        self.boxes.append((text.rstrip(), (x, y, x + ascii_w(text, scale), y + 8 * scale)))

    def pill(self, box, label, color=YELLOW):
        self.round(box, fill=(9, 18, 21))
        x0, y0, x1, y1 = box
        self.ref((x0 + x1 - ref_w(label)) // 2, y0 + 1, label, color)

    def battery(self, x, y):
        self.round((x, y, x + 34, y + 15), fill=(9, 16, 18))
        self.d.rectangle((x + 35, y + 5, x + 38, y + 10), fill=WHITE)
        self.d.rounded_rectangle((x + 2, y + 2, x + 28, y + 13), radius=2, fill=GREEN)

    def card(self, x, y, w, label, value, color):
        self.round((x, y, x + w, y + 48), fill=PANEL)
        self.ref(x + 7, y + 5, label, MUTED)
        self.ascii(x + 7, y + 25, value, color, 2)

    def small_card(self, x, y, w, label, value, color):
        self.round((x, y, x + w, y + 41), fill=PANEL)
        self.ref(x + 6, y + 5, label, MUTED)
        self.ascii(x + 6, y + 23, value, color, 2)

    def audit(self):
        sx0, sy0, sx1, sy1 = SAFE
        for txt, box in self.boxes:
            x0, y0, x1, y1 = box
            if x0 < sx0 or y0 < sy0 or x1 > sx1 or y1 > sy1:
                self.issues.append(f"{self.name}: out-of-safe '{txt}' {box}")
        for i, a in enumerate(self.boxes):
            for b in self.boxes[i + 1 :]:
                if not (a[1][2] <= b[1][0] or b[1][2] <= a[1][0] or a[1][3] <= b[1][1] or b[1][3] <= a[1][1]):
                    self.issues.append(f"{self.name}: overlap '{a[0]}' {a[1]} with '{b[0]}' {b[1]}")
        return self.issues


def page1():
    s = Screen("PAGE1")
    s.ref(16, 11, "协议")
    s.ascii(52, 10, "PD OUT", YELLOW, 2)
    s.ref(184, 11, "电量")
    s.battery(218, 10)
    s.ascii(258, 10, " 86%", GREEN, 2)
    s.hline(30)
    for x, label, value, color in (
        (14, "输入电压", "20.1V ", CYAN),
        (113, "输入电流", "3.25A ", GREEN),
        (212, "功率", "65.3W ", ORANGE),
        (14, "电池电压", "16.4V ", CYAN),
        (113, "电池电流", "-3.8A ", GREEN),
        (212, "电量", " 86%", YELLOW),
    ):
        s.card(x, 36 if x in (14, 113, 212) and label in ("输入电压", "输入电流", "功率") else 84, 92, label, value, color)
    s.ref(16, 132, "芯片")
    s.ascii(16, 151, "38.2C", GREEN, 1)
    s.ref(104, 132, "NTC")
    s.ascii(104, 151, "31.5C", GREEN, 1)
    s.ref(190, 132, "模式")
    s.round((232, 140, 286, 158), fill=(9, 18, 21))
    s.ref(244, 141, "输出", GREEN)
    return s


def page2():
    s = Screen("PAGE2")
    s.round((16, 11, 72, 28), fill=(9, 18, 21))
    s.ascii(24, 15, "PD OUT", YELLOW, 1)
    s.round((80, 11, 126, 28), fill=(9, 18, 21))
    s.ascii(88, 15, "20.1V", CYAN, 1)
    s.ref(220, 12, "电量")
    s.battery(258, 11)
    s.round((94, 35, 226, 119), fill=PANEL)
    s.ref(128, 41, "实时功率")
    s.ascii(115, 63, "65.3W", ORANGE, 3)
    s.ref(110, 96, "输入")
    s.ascii(154, 100, "20.1V/3.25A", CYAN, 1)
    s.small_card(16, 41, 72, "电池", "16.4V", CYAN)
    s.small_card(16, 89, 72, "电流", "-3.8A", GREEN)
    s.small_card(234, 41, 72, "芯片", "38.2C", GREEN)
    s.small_card(234, 89, 72, "NTC", "31.5C", GREEN)
    s.hline(138)
    s.ref(18, 146, "状态")
    s.ref(66, 144, "输出", GREEN)
    s.ref(190, 146, "电量")
    s.ascii(230, 146, "86%", GREEN, 1)
    return s


def page3():
    s = Screen("PAGE3")
    s.round((16, 16, 148, 126), fill=PANEL)
    s.round((172, 16, 304, 126), fill=PANEL)
    s.ref(66, 22, "输入", CYAN)
    s.ref(222, 22, "电池", YELLOW)
    for y, label, value, color in ((49, "电压", "20.1V ", CYAN), (78, "电流", "3.25A ", GREEN), (107, "功率", "65.3W ", ORANGE)):
        s.ref(32, y, label)
        s.ascii(74, y - 5, value, color, 2)
    for y, label, value, color in ((49, "电压", "16.4V ", CYAN), (78, "电流", "-3.8A ", GREEN), (107, "电量", "86% ", YELLOW)):
        s.ref(188, y, label)
        s.ascii(230, y - 5, value, color, 2)
    s.hline(137)
    s.ascii(18, 146, "PD OUT", YELLOW, 1)
    s.ascii(86, 146, "38.2C", GREEN, 1)
    s.ascii(188, 146, "31.5C", GREEN, 1)
    s.ref(270, 144, "输出", GREEN)
    return s


def page4():
    s = Screen("PAGE4")
    s.ascii(16, 13, "PD OUT", YELLOW, 1)
    s.ref(88, 11, "输出", GREEN)
    s.ascii(224, 10, " 86%", GREEN, 2)
    s.battery(270, 11)
    for y in (37, 66, 95, 124):
        s.round((16, y, 304, y + 24), fill=PANEL)
    rows = [
        (43, "输入电压", "20.1V ", CYAN, "输入电流", "3.25A ", GREEN),
        (72, "输出功率", "65.3W ", ORANGE, "电池电压", "16.4V ", CYAN),
        (101, "电池电流", "-3.8A ", GREEN, "芯片温度", "38.2C ", GREEN),
        (130, "NTC温度", "31.5C ", GREEN, "协议", "PD OUT", YELLOW),
    ]
    for y, l1, v1, c1, l2, v2, c2 in rows:
        s.ref(26, y, l1)
        s.ascii(92, y - 4, v1, c1, 2)
        s.ref(166, y, l2)
        s.ascii(232, y - 4, v2, c2, 2)
    return s


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    pages = [("01_page1_cards", page1()), ("02_page2_power", page2()), ("03_page3_columns", page3()), ("04_page4_list", page4())]
    issues = []
    for name, screen in pages:
        issues += screen.audit()
        screen.img.save(OUT / f"{name}_320x172.png")
        screen.img.resize((W * SCALE, H * SCALE), Image.Resampling.NEAREST).save(OUT / f"{name}_1280x688.png")
    sheet = Image.new("RGB", (W * SCALE * 2 + 84, H * SCALE * 2 + 84), (18, 18, 18))
    sd = ImageDraw.Draw(sheet)
    for i, (name, screen) in enumerate(pages):
        x = 28 + (i % 2) * (W * SCALE + 28)
        y = 28 + (i // 2) * (H * SCALE + 28)
        sd.text((x, y), name, fill=WHITE, font=F18)
        sheet.paste(screen.img.resize((W * SCALE, H * SCALE), Image.Resampling.NEAREST), (x, y + 24))
    sheet.save(OUT / "contact_sheet_firmware_4x.png")
    report = OUT / "firmware_layout_audit.txt"
    report.write_text("\n".join(issues) + "\n" if issues else "PASS: firmware text boxes are inside safe area and do not overlap.\n", encoding="utf-8")
    print(OUT / "contact_sheet_firmware_4x.png")
    print(report)
    for issue in issues:
        print(issue)


if __name__ == "__main__":
    main()
