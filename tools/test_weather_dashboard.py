#!/usr/bin/env python3
"""Regression checks for Shenzhen forecast selection and dashboard layout."""

from pathlib import Path


ROOT = Path(__file__).parents[1]
SKETCH = (
    ROOT
    / "SE0398NZ07A0_Shenzhen_Weather"
    / "SE0398NZ07A0_Shenzhen_Weather.ino"
)
FONT = ROOT / "SE0398NZ07A0_Shenzhen_Weather" / "weather_font.h"


def source() -> str:
    return SKETCH.read_text(encoding="utf-8")


def test_panel_palette_uses_verified_a0_codes() -> None:
    assert (
        "enum Color : uint8_t { BLACK = 0, WHITE = 1, YELLOW = 2, RED = 3 };"
        in source()
    )


def test_requests_eight_days_and_aligned_hourly_weather_codes() -> None:
    text = source()
    assert "constexpr uint8_t kHourlyPoints = 8;" in text
    assert "constexpr uint8_t kForecastDays = 7;" in text
    assert "forecast_days=8" in text
    assert "hourly=temperature_2m,weather_code" in text
    assert "precipitation_probability_max,weather_code,sunrise,sunset" in text
    assert "apparent_temperature" not in text
    assert 'hourly["weather_code"].as<JsonArray>()' in text
    assert "next.hourlyCodes[i] = hourlyCodes[source]" in text
    assert "next.hourlyCount == kHourlyPoints" in text


def test_seven_daily_records_skip_today_with_one_source_index() -> None:
    text = source()
    assert "for (uint8_t day = 0; day < kForecastDays; ++day)" in text
    assert "const uint8_t sourceDay = day + 1;" in text
    for field in (
        "temperature_2m_max",
        "temperature_2m_min",
        "precipitation_probability_max",
        "weather_code",
        "time",
    ):
        assert f'daily["{field}"][sourceDay]' in text


def test_update_time_keeps_only_hours_and_minutes() -> None:
    text = source()
    assert "snprintf(gWeather.updated, sizeof(gWeather.updated), \"%c%c:%c%c\"" in text
    assert '"%c%c-%c%c %c%c:%c%c"' not in text


def test_header_date_is_separate_from_future_dates() -> None:
    text = source()
    assert 'char currentDate[11] = "----------";' in text
    assert "snprintf(next.currentDate" in text


def test_current_day_details_are_parsed_for_the_metric_grid() -> None:
    text = source()
    assert "float apparent" not in text
    assert "int precipitationProbability = -1;" in text
    assert 'char sunrise[6] = "--:--";' in text
    assert 'char sunset[6] = "--:--";' in text
    assert 'daily["precipitation_probability_max"][0]' in text
    assert 'daily["sunrise"][0]' in text
    assert 'daily["sunset"][0]' in text
    assert "copyIsoHourMinute" in text


def test_font_contains_big_character() -> None:
    assert "0x5927" in FONT.read_text(encoding="utf-8")


def test_weekday_helpers_are_used() -> None:
    text = source()
    assert "uint8_t weekdayFromIsoDate(const char* date)" in text
    assert "static const char* const labels[] = {" in text
    assert '"周日", "周一", "周二", "周三", "周四", "周五", "周六"' in text


def test_degree_helpers_are_used() -> None:
    text = source()
    assert "drawTemperatureRow" in text
    assert '"%.0fC"' not in text
    assert '"%.1fC"' not in text


def test_approved_header_chart_and_forecast_geometry() -> None:
    text = source()
    assert "constexpr uint16_t kChartTop = 52;" in text
    assert "constexpr int16_t kChartTemperatureY = 62;" in text
    assert "constexpr int16_t kChartIconY = 88;" in text
    assert "constexpr int16_t kChartTimeY = 282;" in text
    assert '"近8小时天气"' not in text
    assert 'drawAsciiRow(row, 22, 21, 2, gIpText, BLACK);' in text
    assert 'drawCjkRow(row, 640, 20, 1, "更新", BLACK);' in text
    assert (
        "drawAsciiRightAlignedRow(row, 746, 21, 2, gWeather.updated, BLACK);"
        in text
    )
    assert '"深圳天气"' not in text
    assert (
        "drawTemperatureRow(row, 135, 66, 8, gWeather.temperature, BLACK);"
        in text
    )
    assert (
        'drawCjkRow(row, 145, 150, 2, weatherText(gWeather.currentCode), BLACK);'
        in text
    )
    assert (
        "const uint16_t right = 16 + ((day + 1) * 736) / kForecastDays;"
        in text
    )
    assert (
        "static_assert(16 + (kForecastDays * 736) / kForecastDays == 752"
        in text
    )


def test_current_details_use_an_enlarged_two_by_two_grid() -> None:
    text = source()
    assert "if (row == 202 || row == 256)" in text
    assert "for (uint16_t x = 16; x <= 316; ++x)" in text
    assert "if (row >= 202 && row <= 310) setPixel(166, BLACK);" in text
    assert 'drawCjkRow(row, 24, 213, 2, "日出", BLACK);' in text
    assert "drawAsciiRow(row, 98, 222, 2, gWeather.sunrise, BLACK);" in text
    assert 'drawCjkRow(row, 174, 213, 2, "日落", BLACK);' in text
    assert "drawAsciiRow(row, 248, 222, 2, gWeather.sunset, BLACK);" in text
    assert 'drawCjkRow(row, 28, 274, 1, "降水概率", BLACK);' in text
    assert "drawAsciiRow(row, 108, 276, 2, precipitation, BLACK);" in text
    assert 'drawCjkRow(row, 178, 267, 2, "湿度", BLACK);' in text
    assert "drawAsciiRow(row, 252, 276, 2, humidity, BLACK);" in text
    assert '"体感"' not in text
    assert "gWeather.apparent" not in text


def test_forecast_labels_and_bands_match_approved_layout() -> None:
    text = source()
    assert "weekdayLabel(gWeather.dates[day])" in text
    assert "drawIconScaledRow(row, iconLeft, 390, 64, 64" in text
    assert "drawTemperatureRow" in text
    assert 'drawCjkRow(row, precipitationX, 508, 1, "降水", BLACK);' in text
    assert (
        "drawAsciiRow(row, precipitationX + 37, 510, 2, rain, BLACK);"
        in text
    )


def test_hourly_chart_labels_every_point() -> None:
    text = source()
    assert "constexpr uint8_t kHourlyPoints = 8;" in text
    assert "drawTemperatureRow" in text
    assert "drawIconScaledRow(row, x - 14, kChartIconY, 28, 28" in text
    assert "char hour[3] = {" in text
    assert "drawAsciiCenteredRow(row, x, kChartTimeY, 2, hour, BLACK);" in text
    assert "kChartTimeYEven" not in text
    assert "kChartTimeYOdd" not in text
    assert "setPixel(x - 2, YELLOW)" in text
    assert "setPixel(x + 2, YELLOW)" in text


if __name__ == "__main__":
    tests = [
        value
        for name, value in sorted(globals().items())
        if name.startswith("test_") and callable(value)
    ]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
