#!/usr/bin/env python3
"""Source contracts for the permanent weather dashboard preview."""

from pathlib import Path


ROOT = Path(__file__).parents[1]
HTML = (ROOT / "weather-preview" / "index.html").read_text(encoding="utf-8")
JS = (ROOT / "weather-preview" / "weather-dashboard.js").read_text(
    encoding="utf-8"
)
CSS = (ROOT / "weather-preview" / "weather-dashboard.css").read_text(
    encoding="utf-8"
)


def test_native_panel_and_responsive_scaling() -> None:
    assert 'width="768" height="552"' in HTML
    assert "aspect-ratio: 768 / 552" in CSS


def test_preview_uses_live_free_api_with_sample_fallback() -> None:
    assert "api.open-meteo.com/v1/forecast" in JS
    assert "forecast_days=8" in JS
    assert "hourly=temperature_2m,weather_code" in JS
    assert "precipitation_probability_max,weather_code,sunrise,sunset" in JS
    assert "SAMPLE_WEATHER" in JS
    assert "renderDashboard" in JS
    assert "AbortController" in JS
    assert "clearTimeout" in JS


def test_preview_matches_approved_labels_and_counts() -> None:
    assert "近8小时天气" not in JS
    assert "更新" in JS
    assert "深圳天气" not in JS
    assert "HOURLY_POINTS = 8" in JS
    assert "FORECAST_DAYS = 7" in JS
    assert "°" in JS
    assert "`降水 ${Math.round(day.rain)}%`" in JS
    assert 'mono(point.time.slice(0, 2), point.x, 282, 16, 900, COLORS.ink, "center")' in JS
    assert "index % 2 === 0 ? 264 : 288" not in JS


def test_current_temperature_has_enlarged_preview_typography() -> None:
    assert "CURRENT_TEMPERATURE_SIZE = 68" in JS
    assert "CURRENT_TEMPERATURE_X = 135" in JS
    assert "CURRENT_TEMPERATURE_Y = 66" in JS
    assert "CURRENT_CONDITION_Y = 150" in JS


def test_current_details_use_two_by_two_metric_layout_without_apparent_temperature() -> None:
    assert 'label("日出", 28, 218, 22, 900)' in JS
    assert "mono(data.sunrise, 80, 220, 20, 800)" in JS
    assert 'label("日落", 178, 218, 22, 900)' in JS
    assert "mono(data.sunset, 230, 220, 20, 800)" in JS
    assert 'label("降水概率", 28, 264, 20, 900)' in JS
    assert 'label("湿度", 178, 264, 22, 900)' in JS
    assert "ctx.fillRect(16, 256, 300, 1)" in JS
    assert "ctx.fillRect(166, 202, 1, 108)" in JS
    assert 'label("体感"' not in JS
    assert "data.apparent" not in JS
    assert "apparent_temperature" not in JS
    assert "data.sunrise" in JS
    assert "data.sunset" in JS
    assert "data.precipitationProbability" in JS
    assert "daily.sunrise[0].slice(11, 16)" in JS
    assert "daily.sunset[0].slice(11, 16)" in JS


def test_hourly_temperature_and_icons_are_aligned_near_panel_top() -> None:
    assert "HOURLY_TEMPERATURE_Y = 62" in JS
    assert "HOURLY_ICON_Y = 88" in JS
    assert "HOURLY_TEMPERATURE_Y," in JS
    assert "point.x - 14, HOURLY_ICON_Y" in JS


if __name__ == "__main__":
    tests = [
        value
        for name, value in sorted(globals().items())
        if name.startswith("test_") and callable(value)
    ]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
