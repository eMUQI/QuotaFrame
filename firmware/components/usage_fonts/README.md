# Montserrat LVGL subsets

This directory contains generated LVGL 9 fonts for the screensaver. The Bold 120 px glyph set is `0123456789:-`. Semibold 13 px and 20 px subsets provide the approved uppercase hint, low-battery label, and date typography; the 13 px subset also carries `%` for the screensaver usage percentages. All files use 4 bits per pixel without compression.

## Provenance

- Upstream: Google Fonts `ofl/montserrat/Montserrat[wght].ttf`
- Commit: `038b637da7b3fd956a4ed93ffc607c3d5e4ce172`
- Source URL: https://raw.githubusercontent.com/google/fonts/038b637da7b3fd956a4ed93ffc607c3d5e4ce172/ofl/montserrat/Montserrat%5Bwght%5D.ttf
- Variable-source SHA-256: `0f7b311b2f3279e4eef9b2f968bcdbab6e28f4daeb1f049f4f278a902bcd82f7`
- Instantiated Bold SHA-256: `478f0487e26a436599bf230797fe74fa2bb901585e87de788395bc0f2d741a69`
- Bold axis: `wght=700`
- Semibold axis: `wght=600`
- fontTools: `4.59.0`
- lv_font_conv: `1.5.3`

## Reproduction

Run from the repository root with Python, Node.js 24+, and pnpm available:

```powershell
$fontToolsDir = Join-Path $env:TEMP 'm5-panel-fonttools-4.59.0'
$sourceFont = Join-Path $env:TEMP 'Montserrat-wght.ttf'
$boldFont = Join-Path $env:TEMP 'Montserrat-Bold.ttf'
$semiboldFont = Join-Path $env:TEMP 'Montserrat-Semibold.ttf'
python -m pip install fonttools==4.59.0 --target $fontToolsDir
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/google/fonts/038b637da7b3fd956a4ed93ffc607c3d5e4ce172/ofl/montserrat/Montserrat%5Bwght%5D.ttf' -OutFile $sourceFont
$env:PYTHONPATH = $fontToolsDir
python -m fontTools.varLib.instancer $sourceFont 'wght=700' --output $boldFont
python -m fontTools.varLib.instancer $sourceFont 'wght=600' --output $semiboldFont
pnpm dlx lv_font_conv@1.5.3 --font $boldFont --symbols '0123456789:-' --size 120 --format lvgl --bpp 4 --no-compress --lv-include 'lvgl.h' --lv-font-name montserrat_bold_120 --output 'firmware/components/usage_fonts/montserrat_bold_120.c'
pnpm dlx lv_font_conv@1.5.3 --font $semiboldFont --symbols ' ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-%' --size 13 --format lvgl --bpp 4 --no-compress --lv-include 'lvgl.h' --lv-font-name montserrat_semibold_13 --output 'firmware/components/usage_fonts/montserrat_semibold_13.c'
pnpm dlx lv_font_conv@1.5.3 --font $semiboldFont --symbols ' ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-' --size 20 --format lvgl --bpp 4 --no-compress --lv-include 'lvgl.h' --lv-font-name montserrat_semibold_20 --output 'firmware/components/usage_fonts/montserrat_semibold_20.c'
```

After generation, replace the machine-specific input and output paths in the generated `Opts` comment with the stable basenames shown in the checked-in file. The font data itself is not modified.
