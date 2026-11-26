<p align="right"><strong>English</strong> · <a href="README.zh_CN.md">简体中文</a></p>

# esp-idf/sensecap-indicator

Device knowledge: [AGENTS.md](../AGENTS.md). Hardware facts are maintained there and in its references.

Build/check: `python3 codecraft.py validate --target esp-idf/sensecap-indicator` from the repository root.

See `target.json` for examples, native interface references and exact toolchain selection. Build results and device observations are separate.

BSP public headers live in `bsp/include/`; implementation and private headers live in `bsp/src/`. Each example has its own `bsp.cmake` and managed dependency manifest. Exported projects include that example’s selected interfaces and source closure.

GX and DX display assemblies have sourced native profiles. Use `--variant gx` or `--variant dx` with context, new or focused Target validation. The full Target validation builds both. The repository build default is GX; exports record their selected variant and use it as the native default. Panel/PCB identity and revision compatibility remain unknown until established separately. See [pinned vendor evidence](../AGENTS.md).

## Target implementation

Device behavior, reference interfaces and example purposes are documented in the [Device implementation references](../AGENTS.md#implementation-references).

### Layout

- `bsp/include/bsp_pins.h` — every pin and timing constant, cited.
- `bsp/src/bsp_io_expander.c` — TCA9535 driver (shadowed output/config regs).
- `bsp/src/bsp_display.c` — ST7701S init + esp_lcd RGB panel + backlight.
- `bsp/src/bsp_button.c` — GPIO 38 button, debounced task.
- `bsp/src/bsp_touch.c` — FT6336U/GT911 auto-probe touch (esp_lcd_touch).
- `sdkconfig.defaults` — shared memory/flash config (symlinked into each
  example).
- `examples/display_color_bars` — 8 vertical color bars + backlight on.
- `examples/button_backlight` — white screen, button toggles backlight.
- `examples/touch_read` — logs touch coordinates as you press the screen.

## Toolchain

The executable version declaration in `target.json` requires ESP-IDF 6.0.2 for esp32s3. Activate that SDK before building.

In an exported application, use `python3 codecraft.py build-project .`; reference-repository validation commands do not replace application lock checks.
