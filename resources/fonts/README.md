# Localization fallback font

`romfs/ui/fallback.bcfnt` is an A4 raster subset of Noto Sans CJK TC Regular,
licensed under the SIL Open Font License 1.1 (see `OFL.txt`). It contains the
catalog's non-ASCII characters, language names, and a default question mark.
The source font includes Chinese, Japanese, Hangul, and Cyrillic coverage.
No Nintendo font data is included.

The UI uses this font only for characters missing from the console's system
font. Measurements and drawing use the same per-character font selection,
and mixed runs share a baseline. The font occupies about 1 MiB of linear
memory and is freed with the UI.

Normal builds use the checked-in BCFNT and need no font tools or network.
After adding catalog characters, regenerate it with Python, Pillow, and
fonttools. Download the immutable `source_url` in `fallback.json`, then run:

```sh
python3 scripts/make-i18n-font.py /path/to/NotoSansCJKtc-Regular.otf
python3 scripts/check-i18n.py
```

The generator checks the source font's SHA-256, refuses missing or clipped
glyphs, and writes BCFNT tables and PICA texture tiles directly. The catalog
check validates the bundled font's coverage and table/texture bounds in CI.
The fallback is a UI subset; arbitrary file names may contain other characters
that require the console's font.
