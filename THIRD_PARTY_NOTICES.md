# Third-party notices

## Waveshare product-specific waveform data

`AI_Usage_Dashboard/epaper_display.cpp` contains the 159-byte full-refresh
waveform table required by the integrated 1.54-inch e-paper controller. The
values and controller initialization sequence were validated against:

- Repository: `waveshareteam/ESP32-C6-ePaper-1.54`
- Release noted by the vendor: `1.1.0` (2026-04-20)
- Reference commit: `c4c47b6a8001f9daa50b38912393c158371e03be`
- Reference file: `02_Example/arduino_v3.3.0/10_LVGL_V9_Test/port_display.cpp`

The Waveshare repository did not contain a root license file at the reference
commit. The MIT license in this repository therefore does not make a separate
license claim over that vendor-supplied waveform data. Before redistributing a
public derivative at scale, confirm the applicable terms with Waveshare or
replace this section with an officially licensed driver dependency.

## Product and company names

Waveshare, Claude, Codex, Gemini, Espressif, and Arduino are names or marks of
their respective owners. Their appearance here describes compatibility only.

