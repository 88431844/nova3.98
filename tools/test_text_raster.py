#!/usr/bin/env python3
"""Regression checks for the dashboard's text-column orientation contract."""

from pathlib import Path
import re


SKETCH = Path(__file__).parents[1] / "SE0398NZ07A0_Shenzhen_Weather" / "SE0398NZ07A0_Shenzhen_Weather.ino"
GENERATOR = Path(__file__).parents[1] / "tools" / "generate_cjk_font.swift"


def cjk_source_column(display_column: int, glyph_width: int) -> int:
    """CJK columns keep the generated left-to-right order."""
    if not 0 <= display_column < glyph_width:
        raise ValueError("display column outside glyph")
    return display_column


def cjk_source_row(display_row: int, glyph_height: int) -> int:
    """CoreGraphics bitmap rows must be flipped into panel top-to-bottom order."""
    if not 0 <= display_row < glyph_height:
        raise ValueError("display row outside glyph")
    return glyph_height - 1 - display_row


def ascii_source_column(display_column: int, glyph_width: int) -> int:
    """ASCII glyphs keep the source order used by their hand-built bitmaps."""
    if not 0 <= display_column < glyph_width:
        raise ValueError("display column outside glyph")
    return display_column


def test_cjk_columns_keep_the_generated_bitmap_order() -> None:
    source = [1, 0, 0, 1, 0, 0, 0, 1]
    rendered = [source[cjk_source_column(x, len(source))] for x in range(len(source))]
    assert rendered == source


def test_cjk_rows_reverse_core_graphics_memory_order() -> None:
    source = [1, 0, 0, 1, 1, 0, 1, 0]
    rendered = [source[cjk_source_row(y, len(source))] for y in range(len(source))]
    assert rendered == list(reversed(source))


def test_ascii_columns_keep_their_original_order() -> None:
    source = [1, 0, 0, 1, 0]
    rendered = [source[ascii_source_column(x, len(source))] for x in range(len(source))]
    assert rendered == source


def test_generator_applies_vertical_cjk_correction_once() -> None:
    generator = GENERATOR.read_text(encoding="utf-8")
    text = SKETCH.read_text(encoding="utf-8")
    assert "bit 7 is display column 0" in generator
    assert "row 0 is the top display row" in generator
    assert "let sourceY = 15 - y" in generator
    assert "pixels[sourceY * 16 + x]" in generator
    assert re.search(r"uint8_t\s+cjkSourceColumn\s*\(", text)
    assert re.search(r"uint8_t\s+asciiSourceColumn\s*\(", text)
    assert "glyphSourceColumn" not in text
    assert "glyphWidth - 1 - displayColumn" not in text
    assert "glyphPixel(codepoint, cjkSourceColumn(gx, 16), gy)" in text
    assert "asciiSourceColumn(gx, 5)" in text
    assert "1 << (7 - (x & 7))" in text


def test_sketch_keeps_ascii_and_panel_coordinates_unmirrored() -> None:
    text = SKETCH.read_text(encoding="utf-8")
    assert "return displayColumn < glyphWidth ? displayColumn : 0;" in text
    assert "asciiSourceColumn(gx, 5)" in text
    assert "constexpr bool kMirrorWeather = false;" in text
    assert "drawCjkRow(row, 145, 142, 2, weatherText" in text
    assert "drawAsciiRow(row, 22, 21, 2, gIpText" in text
    assert "drawAsciiRow(row, 360, 21, 2, gIpText" in text
    assert text.count("drawAsciiRow(row, 22, 21, 2, gIpText") == 1
    assert text.count("drawAsciiRow(row, 360, 21, 2, gIpText") == 1


if __name__ == "__main__":
    tests = [test_cjk_columns_keep_the_generated_bitmap_order, test_cjk_rows_reverse_core_graphics_memory_order, test_ascii_columns_keep_their_original_order, test_generator_applies_vertical_cjk_correction_once, test_sketch_keeps_ascii_and_panel_coordinates_unmirrored]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
