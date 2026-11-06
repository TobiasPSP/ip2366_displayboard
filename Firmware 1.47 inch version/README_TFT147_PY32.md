# PY32F030 1.47 Inch TFT First Port

This folder is a first-pass port from the original 0.96 inch PY32F030 project.

## Panel

- Controller: ST7789P3 compatible
- Logical resolution: 320x172 landscape
- Active-area offset: X=0, Y=34
- Pixel format: RGB565
- MADCTL: 0x70 by default

If the panel lights but the image is shifted, tune `st7735_set_offsets(0, 34)`.
If the image is mirrored, try changing the rotation path in `src/common/st7735.c`
from MADCTL `0x70` to `0xA0`.

## Notes

- The public drawing API is intentionally still named `st7735_*` so the existing
  application and font code can compile with minimal changes.
- The UI layout is not redesigned yet. First target is screen bring-up and basic
  drawing on the 320x172 ST7789P3 panel.
- PY32F030 has 8KB RAM, so this port does not use a full framebuffer or LVGL.
