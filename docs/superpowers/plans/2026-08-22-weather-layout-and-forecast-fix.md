# Weather Layout And Forecast Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render the Shenzhen dashboard with the panel's true white color, three future forecast days, readable large labels, a complete header, and a wider hourly chart, then compile and upload it to the connected ESP8266.

**Architecture:** Keep the existing line-buffer renderer and Open-Meteo JSON flow. Add host-side source-contract tests around the palette, daily offset, font asset, and fixed layout coordinates; make the smallest sketch and font-generator changes that satisfy those contracts. Preserve the stock page and existing uncommitted work.

**Tech Stack:** Arduino C++ / ESP8266, ArduinoJson, Swift CoreText font generator, Python standard-library tests, `arduino-cli`.

---

### Task 1: Add Failing Palette And Forecast Tests

**Files:**
- Create: `tools/test_weather_dashboard.py`
- Modify: `tools/test_weather_palette.py`
- Test: `tools/test_weather_dashboard.py`
- Test: `tools/test_weather_palette.py`

- [ ] **Step 1: Write tests for the physical palette and daily offset**

```python
from pathlib import Path

ROOT = Path(__file__).parents[1]
SKETCH = ROOT / "SE0398NZ07A0_Shenzhen_Weather" / "SE0398NZ07A0_Shenzhen_Weather.ino"


def source() -> str:
    return SKETCH.read_text(encoding="utf-8")


def test_panel_palette_uses_verified_a0_codes() -> None:
    assert "enum Color : uint8_t { BLACK = 0, WHITE = 1, YELLOW = 2, RED = 3 };" in source()


def test_forecast_requests_four_days_and_skips_today() -> None:
    text = source()
    assert "forecast_days=4" in text
    assert "const uint8_t sourceDay = day + 1;" in text
    for field in ("temperature_2m_max", "temperature_2m_min",
                  "precipitation_probability_max", "weather_code", "time"):
        assert f'daily["{field}"][sourceDay]' in text


def test_header_date_is_separate_from_future_dates() -> None:
    text = source()
    assert 'char currentDate[11] = "----------";' in text
    assert "snprintf(next.currentDate" in text
    assert "gWeather.currentDate" in text


if __name__ == "__main__":
    tests = [value for name, value in sorted(globals().items())
             if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
```

- [ ] **Step 2: Correct the old palette test's expected hardware mapping**

Change its enum assertion to:

```python
assert "enum Color : uint8_t { BLACK = 0, WHITE = 1, YELLOW = 2, RED = 3 };" in text
```

- [ ] **Step 3: Run the tests and verify RED**

Run: `python3 tools/test_weather_palette.py && python3 tools/test_weather_dashboard.py`

Expected: FAIL because the sketch still maps `WHITE=3`, requests three days, reads daily index `day`, and has no separate current date.

### Task 2: Correct Weather Data And Palette

**Files:**
- Modify: `SE0398NZ07A0_Shenzhen_Weather/SE0398NZ07A0_Shenzhen_Weather.ino:42-70`
- Modify: `SE0398NZ07A0_Shenzhen_Weather/SE0398NZ07A0_Shenzhen_Weather.ino:352-423`
- Test: `tools/test_weather_dashboard.py`

- [ ] **Step 1: Restore the panel's verified color constants**

```cpp
enum Color : uint8_t { BLACK = 0, WHITE = 1, YELLOW = 2, RED = 3 };
```

- [ ] **Step 2: Store the current date independently**

Add to `WeatherData`:

```cpp
char currentDate[11] = "----------";
```

- [ ] **Step 3: Request four daily entries and copy indices 1 through 3**

Change the URL to `forecast_days=4`, then use one source index for every daily field:

```cpp
for (uint8_t day = 0; day < 3; ++day) {
  const uint8_t sourceDay = day + 1;
  next.high[day] = daily["temperature_2m_max"][sourceDay] | NAN;
  next.low[day] = daily["temperature_2m_min"][sourceDay] | NAN;
  next.rain[day] = daily["precipitation_probability_max"][sourceDay] | -1;
  next.codes[day] = daily["weather_code"][sourceDay] | 3;
  const char* date = daily["time"][sourceDay] | "";
  snprintf(next.dates[day], sizeof(next.dates[day]), "%s", date);
  const char* sunrise = daily["sunrise"][sourceDay] | "";
  const char* sunset = daily["sunset"][sourceDay] | "";
  snprintf(next.sunrise[day], sizeof(next.sunrise[day]), "%.5s", sunrise);
  snprintf(next.sunset[day], sizeof(next.sunset[day]), "%.5s", sunset);
}
```

- [ ] **Step 4: Parse and validate the current date**

After reading `currentTime`, copy its first ten characters when its ISO timestamp is valid. Require the current date and all three future dates and temperatures in `next.valid`; invalid responses must not replace `gWeather`.

```cpp
if (strlen(currentTime) >= 16) {
  snprintf(next.currentDate, sizeof(next.currentDate), "%.10s", currentTime);
}
bool dailyValid = next.currentDate[0] != '-';
for (uint8_t day = 0; day < 3; ++day) {
  dailyValid = dailyValid && next.dates[day][0] != 0 &&
               !isnan(next.high[day]) && !isnan(next.low[day]) &&
               next.rain[day] >= 0;
}
next.valid = dailyValid && !isnan(next.temperature) &&
             next.temperature > -100.0f && next.temperature < 100.0f &&
             next.humidity >= 0;
```

- [ ] **Step 5: Run the data tests and verify GREEN**

Run: `python3 tools/test_weather_dashboard.py`

Expected: all palette and forecast tests pass.

### Task 3: Add Failing Font And Layout Tests

**Files:**
- Modify: `tools/test_weather_dashboard.py`
- Modify: `tools/test_weather_palette.py`
- Test: `tools/test_weather_dashboard.py`
- Test: `tools/test_weather_palette.py`

- [ ] **Step 1: Add source-contract tests for the approved layout**

```python
FONT = ROOT / "SE0398NZ07A0_Shenzhen_Weather" / "weather_font.h"


def test_font_contains_big_character() -> None:
    assert "0x5927" in FONT.read_text(encoding="utf-8")


def test_header_and_chart_use_approved_positions() -> None:
    text = source()
    for declaration in (
        "kChartLeft = 316", "kChartRight = 752",
        "kChartPlotLeft = 340", "kChartPlotRight = 730",
    ):
        assert declaration in text
    assert 'drawAsciiRow(row, 174, 21, 2, gIpText, BLACK);' in text
    assert 'drawCjkRow(row, 398, 20, 1, "更新", BLACK);' in text
    assert 'drawAsciiRow(row, 438, 21, 2, gWeather.updated, BLACK);' in text
    assert 'drawAsciiRow(row, 620, 21, 2, gWeather.currentDate, BLACK);' in text
    assert 'drawAsciiRow(row, 336, 96, 2, "12H TEMP", BLACK);' in text
    assert 'drawAsciiRow(row, 340, 282, 2, gWeather.hourlyTime[0], BLACK);' in text


def test_forecast_labels_and_bands_match_approved_layout() -> None:
    text = source()
    assert '"明天"' in text and '"后天"' in text and '"大后天"' in text
    assert "drawCjkRow(row, left + 16, 328, 2" in text
    assert "drawAsciiRow(row, left + 86, 366, 2" in text
    assert "drawIconRow(row, left + 64, 390" in text
    assert "drawAsciiRow(row, left + 18, 474, 2, high" in text
```

- [ ] **Step 2: Update the existing white-background layout assertions before implementation**

Change `tools/test_weather_palette.py` to expect the approved y=474 temperature bands instead of the old y=468 bands.

- [ ] **Step 3: Run the expanded tests and verify RED**

Run: `python3 tools/test_weather_dashboard.py`

Expected: FAIL because `大` is absent and the sketch still uses the narrow chart and old card labels.

### Task 4: Generate The Missing Glyph And Implement The Layout

**Files:**
- Modify: `tools/generate_cjk_font.swift:5`
- Modify: `SE0398NZ07A0_Shenzhen_Weather/weather_font.h`
- Modify: `SE0398NZ07A0_Shenzhen_Weather/SE0398NZ07A0_Shenzhen_Weather.ino:42-49`
- Modify: `SE0398NZ07A0_Shenzhen_Weather/SE0398NZ07A0_Shenzhen_Weather.ino:710-789`
- Test: `tools/test_weather_dashboard.py`

- [ ] **Step 1: Add `大` to the font generator and regenerate the header**

Insert `大` into the generator's character string, then run:

```bash
swift tools/generate_cjk_font.swift SE0398NZ07A0_Shenzhen_Weather/weather_font.h
```

Expected: output reports 47 glyphs and `weather_font.h` contains codepoint `0x5927`.

- [ ] **Step 2: Widen the chart constants**

```cpp
constexpr uint16_t kChartLeft = 316;
constexpr uint16_t kChartRight = 752;
constexpr uint16_t kChartTop = 76;
constexpr uint16_t kChartBottom = 310;
constexpr uint16_t kChartPlotLeft = 340;
constexpr uint16_t kChartPlotRight = 730;
constexpr int16_t kChartPlotTop = 126;
constexpr int16_t kChartPlotBottom = 266;
```

- [ ] **Step 3: Enlarge the chart title and endpoint times**

Use `12H TEMP` at x=336, y=96, scale 2. Put the first endpoint at x=340 and compute the last endpoint's x from a fixed width of 60 pixels so both remain inside x=730.

```cpp
drawAsciiRow(row, 336, 96, 2, "12H TEMP", BLACK);
drawAsciiRow(row, 340, 282, 2, gWeather.hourlyTime[0], BLACK);
drawAsciiRow(row, 670, 282, 2,
             gWeather.hourlyTime[gWeather.hourlyCount - 1], BLACK);
```

- [ ] **Step 4: Rebuild the weather header and current-weather area**

Render the four approved header groups. Keep the icon/temperature above y=226 and place apparent temperature and humidity below that separator:

```cpp
drawCjkRow(row, 22, 18, 2, "深圳天气", BLACK);
drawAsciiRow(row, 174, 21, 2, gIpText, BLACK);
drawCjkRow(row, 398, 20, 1, "更新", BLACK);
drawAsciiRow(row, 438, 21, 2, gWeather.updated, BLACK);
drawAsciiRow(row, 620, 21, 2, gWeather.currentDate, BLACK);
drawIconRow(row, 30, 102, gWeather.currentCode);
drawAsciiRow(row, 145, 106, 4, current, BLACK);
drawCjkRow(row, 145, 164, 2, weatherText(gWeather.currentCode), BLACK);
```

At row 226, draw a black separator from x=28 through x=304. Put `体感` at x=34 and its value at x=98; put `湿度` at x=170 and its value at x=234. Remove the old update line at y=294.

- [ ] **Step 5: Rebuild the forecast cards with fixed non-overlapping bands**

```cpp
const char* label = day == 0 ? "明天" : (day == 1 ? "后天" : "大后天");
drawCjkRow(row, left + 16, 328, 2, label, BLACK);
drawAsciiRow(row, left + 86, 366, 2, gWeather.dates[day] + 5, BLACK);
drawIconRow(row, left + 64, 390, gWeather.codes[day]);
drawAsciiRow(row, left + 18, 474, 2, high, BLACK);
drawAsciiRow(row, left + 78, 474, 2, "/", BLACK);
drawAsciiRow(row, left + 120, 474, 2, low, BLACK);
drawCjkRow(row, left + 18, 510, 1, "降水", BLACK);
drawAsciiRow(row, left + 88, 512, 2, rain, BLACK);
```

- [ ] **Step 6: Run the dashboard tests and verify GREEN**

Run: `python3 tools/test_weather_dashboard.py`

Expected: all tests pass.

### Task 5: Full Verification, Compile, And Upload

**Files:**
- Modify: `README.md`
- Test: `tools/test_text_raster.py`
- Test: `tools/test_weather_icons.py`
- Test: `tools/test_weather_palette.py`
- Test: `tools/test_weather_dashboard.py`

- [ ] **Step 1: Update the README behavior summary**

State that the weather page shows the next three days, a 12-hour chart, IP address, and API update time on the verified white background.

- [ ] **Step 2: Run all host regressions**

Run:

```bash
python3 tools/test_text_raster.py
python3 tools/test_weather_icons.py
python3 tools/test_weather_palette.py
python3 tools/test_weather_dashboard.py
```

Expected: every test prints only PASS lines and exits 0.

- [ ] **Step 3: Compile the ESP8266 sketch**

Run:

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 \
  --output-dir /tmp/nova14-weather-build \
  SE0398NZ07A0_Shenzhen_Weather
```

Expected: exit 0 with flash and RAM usage below the NodeMCU limits.

- [ ] **Step 4: Identify the target ESP8266 without writing firmware**

List USB serial devices and probe each candidate with the ESP8266 core's bundled esptool `chip_id` operation. Select only a port that reports an ESP8266; if multiple ports report ESP8266, stop and ask the user which physical board is connected to the display.

- [ ] **Step 5: Upload the compiled weather sketch**

Run:

```bash
arduino-cli upload --fqbn esp8266:esp8266:nodemcuv2 \
  --input-dir /tmp/nova14-weather-build \
  -p "$weather_port" \
  SE0398NZ07A0_Shenzhen_Weather
```

Expected: esptool connects, writes, verifies, and resets the board successfully.

- [ ] **Step 6: Inspect startup output when the port is readable**

Read at 115200 baud and verify `SE0398NZ07A0 BYD + Shenzhen dashboard`, successful Wi-Fi/weather fetch, and display refresh. If the serial monitor cannot be opened after upload, report upload success separately from physical display verification.
