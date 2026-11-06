from pathlib import Path
import re

src = Path("资料/显示参考项目/USER/Driver/spi/spi_lcd_code_table.c")
text = src.read_text(encoding="gbk", errors="ignore")
part = text[text.find("const CHS_TBL_16x16"):text.find("const CHS_TBL_16x32")]
entries = re.findall(r'"(.)"\s*,\s*((?:0x[0-9A-Fa-f]{2},?\s*){32})', part)

needed = "电压温度输出输入功率无非快充端口正常芯片关闭异常小流模式可损毁失联"
seen = {}
for ch, data in entries:
    vals = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", data)]
    if ch in needed and len(vals) == 32 and ch not in seen:
        seen[ch] = vals

lines = [
    "/* Generated from display reference project chs_16x16 table. */",
    "#ifndef FONT_ZH16_REF_H",
    "#define FONT_ZH16_REF_H",
    "",
    "static const zh_glyph16_t k_ref_zh_glyphs[] = {",
]

for ch in needed:
    vals = seen.get(ch)
    if vals is not None:
        body = ",".join(f"0x{v:02X}" for v in vals)
        lines.append(f"  {{0x{ord(ch):04X}, {{{body}}}}},")

lines.extend(["};", "", "#endif"])
Path("src/common/font_zh16_ref.h").write_text("\n".join(lines) + "\n", encoding="ascii")
print(f"wrote {len(seen)} glyphs")
