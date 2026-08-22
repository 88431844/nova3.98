#!/usr/bin/env python3
"""Validate the generated fixed-size icon masks used by the ESP8266 sketch."""

from pathlib import Path
import re


HEADER = Path(__file__).parents[1] / "SE0398NZ07A0_Shenzhen_Weather" / "weather_icons.h"
SOURCE = Path(__file__).parents[1] / "tools" / "iconfont_weather_paths.json"
EXPECTED = (
    "Sunny",
    "PartlyCloudy",
    "Cloudy",
    "Rain",
    "HeavyRain",
    "Snow",
    "Storm",
    "Fog",
)
BYTES_PER_MASK = 96 * 96 // 8


def test_header_dimensions() -> None:
    text = HEADER.read_text(encoding="utf-8")
    assert "kWeatherIconWidth = 96" in text
    assert "kWeatherIconHeight = 96" in text
    assert "kWeatherIconBytesPerRow = 12" in text


def test_approved_iconfont_family_ids_are_recorded() -> None:
    text = SOURCE.read_text(encoding="utf-8")
    for source_id in (
        "3010907",
        "3010910",
        "3010909",
        "3010912",
        "3010915",
        "3010914",
        "3010916",
        "3010924",
        "3010937",
    ):
        assert source_id in text


def test_masks_are_complete_and_non_empty() -> None:
    text = HEADER.read_text(encoding="utf-8")
    for name in EXPECTED:
        match = re.search(
            rf"kWeatherIcon{name}\[(\d+)\].*?= \{{(.*?)\n\}};",
            text,
            re.DOTALL,
        )
        assert match, f"missing mask {name}"
        assert int(match.group(1)) == BYTES_PER_MASK
        values = re.findall(r"0x[0-9A-F]{2}", match.group(2))
        assert len(values) == BYTES_PER_MASK
        assert any(value != "0x00" for value in values)


if __name__ == "__main__":
    tests = [
        test_header_dimensions,
        test_approved_iconfont_family_ids_are_recorded,
        test_masks_are_complete_and_non_empty,
    ]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
