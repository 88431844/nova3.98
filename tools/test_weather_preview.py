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
    assert "SAMPLE_WEATHER" in JS
    assert "renderDashboard" in JS
    assert "AbortController" in JS
    assert "clearTimeout" in JS


def test_preview_matches_approved_labels_and_counts() -> None:
    assert "近8小时天气" in JS
    assert "更新" in JS
    assert "深圳天气" not in JS
    assert "HOURLY_POINTS = 8" in JS
    assert "FORECAST_DAYS = 7" in JS
    assert "°" in JS
    assert "`降水 ${Math.round(day.rain)}%`" in JS
    assert 'mono(point.time.slice(0, 2), point.x, 282, 16, 900, COLORS.ink, "center")' in JS
    assert "index % 2 === 0 ? 264 : 288" not in JS


if __name__ == "__main__":
    tests = [
        value
        for name, value in sorted(globals().items())
        if name.startswith("test_") and callable(value)
    ]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
