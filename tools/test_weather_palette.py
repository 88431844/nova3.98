#!/usr/bin/env python3
"""Regression checks for the SE0398NZ07A0 panel color codes."""

from pathlib import Path


SKETCH = Path(__file__).parents[1] / "SE0398NZ07A0_Shenzhen_Weather" / "SE0398NZ07A0_Shenzhen_Weather.ino"


def test_panel_palette_is_black_yellow_red_white() -> None:
    text = SKETCH.read_text(encoding="utf-8")
    assert "enum Color : uint8_t { BLACK = 0, WHITE = 1, YELLOW = 2, RED = 3 };" in text


def test_weather_icon_colors_follow_conditions() -> None:
    text = SKETCH.read_text(encoding="utf-8")
    assert "if (code <= 3)" in text
    assert "color = YELLOW;" in text
    assert "if (code >= 95)" in text
    assert "color = RED;" in text
    assert "if (code == 45 || code == 48)" in text


def test_clear_weather_uses_sunny_icon() -> None:
    text = SKETCH.read_text(encoding="utf-8")
    assert "if (code == 0)" in text
    assert "return kWeatherIconSunny;" in text
    assert "return code <= 2 ? kWeatherIconPartlyCloudy : kWeatherIconCloudy;" in text


def test_weather_text_is_black_on_white() -> None:
    text = SKETCH.read_text(encoding="utf-8")
    assert "fillRow(WHITE);" in text
    assert "drawCjkRow(row, 145, 150, 2, weatherText(gWeather.currentCode), BLACK);" in text
    assert (
        "drawTemperatureRow(row, temperatureX, 472, 2, gWeather.high[day], BLACK);"
        in text
    )
    assert (
        "drawTemperatureRow(row, temperatureX, 472, 2, gWeather.low[day], BLACK);"
        in text
    )
    assert "gWeather.rain[day] >= 60 ? RED : YELLOW" not in text


if __name__ == "__main__":
    tests = [test_panel_palette_is_black_yellow_red_white, test_weather_icon_colors_follow_conditions, test_clear_weather_uses_sunny_icon, test_weather_text_is_black_on_white]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
