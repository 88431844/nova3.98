# Eight-Hour And Seven-Day Weather Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the approved 768x552 weather dashboard with eight hourly icon samples, seven weekday columns, degree marks, a permanent live browser preview, and upload it to the connected ESP8266.

**Architecture:** Keep the ESP8266 line-buffer renderer and Open-Meteo transport. Expand `WeatherData`, add small pure helpers for weekday and temperature rendering, and scale one generated Iconfont mask family into three target sizes. Add a dependency-free browser preview that mirrors the panel geometry and consumes the same Open-Meteo fields.

**Tech Stack:** Arduino C++ / ESP8266, ArduinoJson, Swift CoreGraphics/CoreText generators, Python standard-library tests, standalone HTML/CSS/JavaScript Canvas/SVG, `arduino-cli`.

---

## File Map

- `SE0398NZ07A0_Shenzhen_Weather/SE0398NZ07A0_Shenzhen_Weather.ino`: Open-Meteo parsing, weekday/degree helpers, scaled icon renderer, and fixed panel layout.
- `SE0398NZ07A0_Shenzhen_Weather/weather_font.h`: generated CJK glyph asset.
- `SE0398NZ07A0_Shenzhen_Weather/weather_icons.h`: generated 96x96 Iconfont masks.
- `tools/generate_cjk_font.swift`: authoritative glyph list.
- `tools/iconfont_weather_paths.json`: authoritative selected Iconfont source paths and source identifiers.
- `tools/generate_weather_icons.swift`: SVG parsing and square-mask generation.
- `tools/test_weather_dashboard.py`: source-contract tests for API alignment, weekday logic, degree rendering, and coordinates.
- `tools/test_weather_icons.py`: generated mask size/name/source-family tests.
- `tools/test_text_raster.py`: text orientation and updated weather-layout assertions.
- `tools/test_weather_preview.py`: permanent preview structure/data-contract checks.
- `weather-preview/index.html`: browser shell and native 768x552 panel surface.
- `weather-preview/weather-dashboard.js`: Open-Meteo fetch, validation, sample fallback, and panel rendering.
- `weather-preview/weather-dashboard.css`: page shell and aspect-ratio scaling only.
- `README.md`: concise final dashboard and preview instructions.

Do not stage or modify `SE0398NZ07A0_NodeMCU_Test/SE0398NZ07A0_NodeMCU_Test.ino` or `.superpowers/`.

### Task 1: Add Failing Dashboard Data And Geometry Tests

**Files:**
- Modify: `tools/test_weather_dashboard.py`
- Modify: `tools/test_text_raster.py`

- [ ] **Step 1: Replace the old 12-hour/three-day assertions with the approved contracts**

Add these focused checks to `tools/test_weather_dashboard.py`:

```python
def test_requests_eight_days_and_aligned_hourly_weather_codes() -> None:
    text = source()
    assert "constexpr uint8_t kHourlyPoints = 8;" in text
    assert "constexpr uint8_t kForecastDays = 7;" in text
    assert "forecast_days=8" in text
    assert "hourly=temperature_2m,weather_code" in text
    assert 'hourly["weather_code"].as<JsonArray>()' in text
    assert "next.hourlyCodes[i] = hourlyCodes[source]" in text
    assert "next.hourlyCount == kHourlyPoints" in text


def test_seven_daily_records_skip_today_with_one_source_index() -> None:
    text = source()
    assert "for (uint8_t day = 0; day < kForecastDays; ++day)" in text
    assert "const uint8_t sourceDay = day + 1;" in text
    for field in ("temperature_2m_max", "temperature_2m_min",
                  "precipitation_probability_max", "weather_code", "time"):
        assert f'daily["{field}"][sourceDay]' in text


def test_weekday_and_degree_helpers_are_used() -> None:
    text = source()
    assert "uint8_t weekdayFromIsoDate(const char* date)" in text
    assert 'static const char* const labels[] = {' in text
    assert '"周日", "周一", "周二", "周三", "周四", "周五", "周六"' in text
    assert "drawTemperatureRow" in text
    assert '"%.0fC"' not in text
    assert '"%.1fC"' not in text


def test_approved_header_chart_and_forecast_geometry() -> None:
    text = source()
    assert "constexpr uint16_t kChartTop = 52;" in text
    assert "constexpr int16_t kChartTemperatureY = 94;" in text
    assert "constexpr int16_t kChartIconY = 120;" in text
    assert "constexpr int16_t kChartTimeYEven = 264;" in text
    assert "constexpr int16_t kChartTimeYOdd = 288;" in text
    assert 'drawCjkRow(row, 336, 56, 2, "近", BLACK);' in text
    assert 'drawAsciiRow(row, 370, 56, 2, "8", BLACK);' in text
    assert 'drawCjkRow(row, 384, 56, 2, "小时天气", BLACK);' in text
    assert 'drawAsciiRow(row, 22, 21, 2, gIpText, BLACK);' in text
    assert 'drawCjkRow(row, 640, 20, 1, "更新", BLACK);' in text
    assert "drawAsciiRightAlignedRow(row, 746, 21, 2, gWeather.updated, BLACK);" in text
    assert '"深圳天气"' not in text
    assert "const uint16_t right = 16 + ((day + 1) * 736) / kForecastDays;" in text
    assert "static_assert(16 + (kForecastDays * 736) / kForecastDays == 752" in text
```

Update `tools/test_text_raster.py` to expect the weather IP at `x=22`, keep the stock IP at `x=360`, and remove its old `x=174` weather assertion.

- [ ] **Step 2: Run the dashboard/text tests and verify RED**

Run:

```bash
python3 tools/test_weather_dashboard.py
python3 tools/test_text_raster.py
```

Expected: failures for `kHourlyPoints=12`, `forecast_days=4`, absent hourly weather codes, old header/layout coordinates, and missing degree/weekday helpers.

- [ ] **Step 3: Commit the failing contracts only**

```bash
git add tools/test_weather_dashboard.py tools/test_text_raster.py
git commit -m "test: define eight-hour seven-day dashboard contracts"
```

### Task 2: Replace Iconfont Assets And Add Glyph Coverage

**Files:**
- Modify: `tools/iconfont_weather_paths.json`
- Modify: `tools/generate_weather_icons.swift`
- Modify: `tools/generate_cjk_font.swift`
- Modify: `tools/test_weather_icons.py`
- Modify: `SE0398NZ07A0_Shenzhen_Weather/weather_icons.h`
- Modify: `SE0398NZ07A0_Shenzhen_Weather/weather_font.h`

- [ ] **Step 1: Write failing source-family and generated-size tests**

Change `tools/test_weather_icons.py` to require a 96x96 mask (`1152` bytes), these symbols, and the selected Iconfont IDs in the JSON source:

```python
SOURCE = Path(__file__).parents[1] / "tools" / "iconfont_weather_paths.json"
EXPECTED = ("Sunny", "PartlyCloudy", "Cloudy", "Rain", "HeavyRain",
            "Snow", "Storm", "Fog")
BYTES_PER_MASK = 96 * 96 // 8

def test_header_dimensions() -> None:
    text = HEADER.read_text(encoding="utf-8")
    assert "kWeatherIconWidth = 96" in text
    assert "kWeatherIconHeight = 96" in text
    assert "kWeatherIconBytesPerRow = 12" in text

def test_approved_iconfont_family_ids_are_recorded() -> None:
    text = SOURCE.read_text(encoding="utf-8")
    for source_id in ("3010907", "3010910", "3010909", "3010912",
                      "3010915", "3010914", "3010916", "3010924", "3010937"):
        assert source_id in text
```

- [ ] **Step 2: Run the icon tests and verify RED**

Run: `python3 tools/test_weather_icons.py`

Expected: failure because the generated masks are currently 96x78 and the selected family IDs are absent.

- [ ] **Step 3: Store and map the approved source records**

Replace the active JSON records with the Iconfont `天气库 · 方案一` paths captured in the approved visual reference and record each selected `sourceId`. The approved preview directly contains the sunny, cloudy, and rain SVG paths; use those exact paths for the primary visible states. Retain the existing snow mask as a compatibility fallback because the approved source-ID list does not include a snow glyph. Map generator symbols as follows:

```swift
let specs = [
  IconSpec(symbol: "Sunny", sourceName: "晴天"),
  IconSpec(symbol: "PartlyCloudy", sourceName: "多云"),
  IconSpec(symbol: "Cloudy", sourceName: "阴"),
  IconSpec(symbol: "Rain", sourceName: "小雨"),
  IconSpec(symbol: "HeavyRain", sourceName: "大雨"),
  IconSpec(symbol: "Snow", sourceName: "大雪兼容"),
  IconSpec(symbol: "Storm", sourceName: "打雷"),
  IconSpec(symbol: "Fog", sourceName: "雾"),
]
```

Change the generator canvas to a square mask:

```swift
let canvasWidth = 96
let canvasHeight = 96
```

Add the approved glyphs to `tools/generate_cjk_font.swift`:

```swift
let characters = Array("深圳天气今明后大温度湿体感晴多云阴小雨中雷雪雾最高低降概率日出落更新失败当前星期一二三四五六七近小时周").map(String.init)
```

- [ ] **Step 4: Regenerate both headers**

Run:

```bash
swift tools/generate_weather_icons.swift tools/iconfont_weather_paths.json SE0398NZ07A0_Shenzhen_Weather/weather_icons.h
swift tools/generate_cjk_font.swift SE0398NZ07A0_Shenzhen_Weather/weather_font.h
```

Expected: eight non-empty 96x96 weather masks and generated glyph entries for `近`, `小`, `时`, and `周`.

- [ ] **Step 5: Run icon and text tests**

Run:

```bash
python3 tools/test_weather_icons.py
python3 tools/test_text_raster.py
```

Expected: icon tests pass; dashboard-specific text assertions may remain red until Tasks 3-4.

- [ ] **Step 6: Commit generated assets and their sources**

```bash
git add tools/iconfont_weather_paths.json tools/generate_weather_icons.swift tools/generate_cjk_font.swift tools/test_weather_icons.py SE0398NZ07A0_Shenzhen_Weather/weather_icons.h SE0398NZ07A0_Shenzhen_Weather/weather_font.h
git commit -m "feat: adopt weather library iconfont family"
```

### Task 3: Implement Eight-Hour And Seven-Day Weather Data

**Files:**
- Modify: `SE0398NZ07A0_Shenzhen_Weather/SE0398NZ07A0_Shenzhen_Weather.ino`
- Test: `tools/test_weather_dashboard.py`

- [ ] **Step 1: Expand fixed data structures**

Use these constants and aligned arrays:

```cpp
constexpr uint8_t kHourlyPoints = 8;
constexpr uint8_t kForecastDays = 7;

float high[kForecastDays] = {0};
float low[kForecastDays] = {0};
int rain[kForecastDays] = {0};
int codes[kForecastDays] = {3};
char dates[kForecastDays][11] = {{0}};
float hourlyTemperature[kHourlyPoints] = {0};
int hourlyCodes[kHourlyPoints] = {3};
char hourlyTime[kHourlyPoints][6] = {{0}};
```

- [ ] **Step 2: Expand the Open-Meteo request and parse aligned records**

Use `forecast_days=8`, add `weather_code` to `hourly`, parse `hourlyCodes`, and require exactly eight valid hourly records:

```cpp
const JsonArray hourlyCodes = hourly["weather_code"].as<JsonArray>();
for (uint8_t i = 0; i < kHourlyPoints; ++i) {
  const size_t source = firstHour + i;
  if (source >= hourlyTemperatures.size() || source >= hourlyCodes.size() ||
      source >= hourlyTimes.size()) break;
  next.hourlyTemperature[i] = hourlyTemperatures[source] | NAN;
  next.hourlyCodes[i] = hourlyCodes[source] | -1;
  // Copy HH:MM exactly as the existing parser does.
  if (isnan(next.hourlyTemperature[i]) || next.hourlyCodes[i] < 0) break;
  next.hourlyCount = i + 1;
}
```

Loop `day < kForecastDays`, retain `sourceDay = day + 1`, and require every daily record plus `next.hourlyCount == kHourlyPoints` before replacing `gWeather`. Store only `HH:MM` in `gWeather.updated`:

```cpp
snprintf(gWeather.updated, sizeof(gWeather.updated), "%c%c:%c%c",
         currentTime[11], currentTime[12], currentTime[14], currentTime[15]);
```

- [ ] **Step 3: Add deterministic weekday helpers**

Use Sakamoto's Gregorian weekday calculation without timezone or libc dependencies:

```cpp
uint8_t weekdayFromIsoDate(const char* date) {
  int year = 0, month = 0, day = 0;
  if (!date || sscanf(date, "%4d-%2d-%2d", &year, &month, &day) != 3) return 0;
  static const uint8_t offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month < 3) --year;
  return static_cast<uint8_t>((year + year / 4 - year / 100 + year / 400 +
                               offsets[month - 1] + day) % 7);
}

const char* weekdayLabel(const char* date) {
  static const char* const labels[] = {
      "周日", "周一", "周二", "周三", "周四", "周五", "周六"};
  return labels[weekdayFromIsoDate(date)];
}
```

- [ ] **Step 4: Run dashboard tests**

Run: `python3 tools/test_weather_dashboard.py`

Expected: data, source-index, update-time, and weekday assertions pass; remaining rendering assertions fail until Task 4.

- [ ] **Step 5: Commit the data changes**

```bash
git add SE0398NZ07A0_Shenzhen_Weather/SE0398NZ07A0_Shenzhen_Weather.ino tools/test_weather_dashboard.py
git commit -m "feat: fetch eight hourly and seven daily forecasts"
```

### Task 4: Implement The Approved Firmware Renderer

**Files:**
- Modify: `SE0398NZ07A0_Shenzhen_Weather/SE0398NZ07A0_Shenzhen_Weather.ino`
- Test: `tools/test_weather_dashboard.py`
- Test: `tools/test_weather_palette.py`
- Test: `tools/test_text_raster.py`

- [ ] **Step 1: Add centered/right-aligned ASCII and degree helpers**

Use the renderer's 6-pixel character advance:

```cpp
uint16_t asciiTextWidth(const char* text, uint8_t scale) {
  return strlen(text) * 6 * scale;
}

void drawAsciiRightAlignedRow(uint16_t row, uint16_t right, uint16_t y,
                              uint8_t scale, const char* text, Color color) {
  drawAsciiRow(row, right - asciiTextWidth(text, scale), y, scale, text, color);
}

void drawDegreeRow(uint16_t row, uint16_t x, uint16_t y, uint8_t scale,
                   Color color) {
  const uint8_t radius = max<uint8_t>(1, scale);
  const int16_t cy = y + radius;
  for (int16_t dx = -radius; dx <= radius; ++dx) {
    if (row == cy - radius || row == cy + radius) setPixel(x + radius + dx, color);
  }
  if (row > cy - radius && row < cy + radius) {
    setPixel(x, color);
    setPixel(x + radius * 2, color);
  }
}

void drawTemperatureRow(uint16_t row, uint16_t x, uint16_t y, uint8_t scale,
                        float value, Color color) {
  char number[8];
  snprintf(number, sizeof(number), "%.0f", value);
  drawAsciiRow(row, x, y, scale, number, color);
  drawDegreeRow(row, x + asciiTextWidth(number, scale), y, scale, color);
}
```

- [ ] **Step 2: Add a scaled icon row renderer**

Sample the 96x96 mask without allocating another bitmap:

```cpp
void drawIconScaledRow(uint16_t row, uint16_t left, uint16_t top,
                       uint16_t width, uint16_t height, int code) {
  if (row < top || row >= top + height) return;
  Color color = BLACK;
  const uint8_t* icon = weatherIconForCode(code, color);
  const uint16_t sourceY = (row - top) * kWeatherIconHeight / height;
  for (uint16_t x = 0; x < width; ++x) {
    const uint16_t sourceX = x * kWeatherIconWidth / width;
    const uint8_t packed = pgm_read_byte(
        icon + sourceY * kWeatherIconBytesPerRow + sourceX / 8);
    if (packed & (0x80 >> (sourceX & 7))) setPixel(left + x, color);
  }
}
```

Map WMO 82 and stronger rain to `HeavyRain`; keep precipitation/thunder red and clear/cloud/fog yellow.

- [ ] **Step 3: Rebuild header and current-weather coordinates**

Render IP at `x=22`, center date around panel midpoint using `asciiTextWidth`, render `更新` at `x=640`, and right-align `HH:MM` to `x=746`. Draw the shared frame at `y=52`, the 84x84 current icon at `(36,80)`, current temperature at `(145,78)`, condition at `(145,142)`, separator `y=210`, and enlarged detail labels at `y=258`.

- [ ] **Step 4: Rebuild the eight-hour chart**

Define the approved constants, render the mixed title as three calls, place scale-2 temperatures with degree marks at `y=94`, 28x28 icons at `y=120`, guide lines, red line, yellow points, and alternating scale-2 times at `y=264/288`. Clamp the time label origin to `kChartLeft + 2` and `kChartRight - asciiTextWidth(time, 2) - 2`.

- [ ] **Step 5: Rebuild seven equal forecast columns**

For every `day < kForecastDays`, compute:

```cpp
const uint16_t left = 16 + (day * 736) / kForecastDays;
const uint16_t right = 16 + ((day + 1) * 736) / kForecastDays;
const uint16_t center = (left + right) / 2;
```

Add a compile-time boundary check beside the layout constants:

```cpp
static_assert(16 + (kForecastDays * 736) / kForecastDays == 752,
              "Forecast strip must align with the main frame");
```

Draw the black cell frame, centered weekday at `y=326`, centered `MM-DD` at `y=362`, centered 64x64 icon at `y=390`, centered high/low degree values at `y=472`, and centered compact `降水 NN%` at `y=508`.

- [ ] **Step 6: Run all focused firmware source tests**

Run:

```bash
python3 tools/test_weather_dashboard.py
python3 tools/test_weather_palette.py
python3 tools/test_text_raster.py
python3 tools/test_weather_icons.py
```

Expected: all pass.

- [ ] **Step 7: Commit the renderer**

```bash
git add SE0398NZ07A0_Shenzhen_Weather/SE0398NZ07A0_Shenzhen_Weather.ino tools/test_weather_dashboard.py tools/test_weather_palette.py tools/test_text_raster.py
git commit -m "feat: render approved weather dashboard layout"
```

### Task 5: Build The Permanent Browser Preview

**Files:**
- Create: `weather-preview/index.html`
- Create: `weather-preview/weather-dashboard.css`
- Create: `weather-preview/weather-dashboard.js`
- Create: `tools/test_weather_preview.py`

- [ ] **Step 1: Write failing preview contract tests**

Create `tools/test_weather_preview.py`:

```python
from pathlib import Path

ROOT = Path(__file__).parents[1]
HTML = (ROOT / "weather-preview" / "index.html").read_text(encoding="utf-8")
JS = (ROOT / "weather-preview" / "weather-dashboard.js").read_text(encoding="utf-8")
CSS = (ROOT / "weather-preview" / "weather-dashboard.css").read_text(encoding="utf-8")

def test_native_panel_and_responsive_scaling() -> None:
    assert 'width="768" height="552"' in HTML
    assert "aspect-ratio: 768 / 552" in CSS

def test_preview_uses_live_free_api_with_sample_fallback() -> None:
    assert "api.open-meteo.com/v1/forecast" in JS
    assert "forecast_days=8" in JS
    assert "hourly=temperature_2m,weather_code" in JS
    assert "SAMPLE_WEATHER" in JS
    assert "renderDashboard" in JS

def test_preview_matches_approved_labels_and_counts() -> None:
    assert "近8小时天气" in JS
    assert "更新" in JS
    assert "深圳天气" not in JS
    assert "HOURLY_POINTS = 8" in JS
    assert "FORECAST_DAYS = 7" in JS
    assert "°" in JS
```

- [ ] **Step 2: Run the preview test and verify RED**

Run: `python3 tools/test_weather_preview.py`

Expected: failure because `weather-preview/` does not exist.

- [ ] **Step 3: Implement the dependency-free preview**

`index.html` must contain a native canvas and external CSS/JS:

```html
<main>
  <p id="data-status" role="status">正在获取实时天气</p>
  <div class="panel-frame">
    <canvas id="weather-panel" width="768" height="552"></canvas>
  </div>
</main>
<script src="weather-dashboard.js"></script>
```

The CSS must scale only the complete panel:

```css
.panel-frame { width: min(100%, 960px); aspect-ratio: 768 / 552; }
#weather-panel { display: block; width: 100%; height: 100%; image-rendering: pixelated; }
```

The JavaScript must define `HOURLY_POINTS = 8`, `FORECAST_DAYS = 7`, deterministic `SAMPLE_WEATHER`, a validated Open-Meteo mapper, weekday labels, Iconfont SVG path data from the approved family, and `renderDashboard(data)`. Fetch live data on load; on failure set status to `示例数据 · 实时接口暂不可用` and render the sample.

- [ ] **Step 4: Run the preview source test**

Run: `python3 tools/test_weather_preview.py`

Expected: all pass.

- [ ] **Step 5: Commit the preview**

```bash
git add weather-preview tools/test_weather_preview.py
git commit -m "feat: add live weather panel preview"
```

### Task 6: Documentation, Full Verification, Compile, And Upload

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update the README dashboard description**

State that the weather page uses free/keyless Open-Meteo, shows eight hourly temperatures with hourly condition icons, and displays seven future weekday columns. Add:

```text
网页坐标预览位于 weather-preview/index.html；可直接打开，联网时显示深圳实时天气，接口不可用时显示明确标记的示例数据。
```

- [ ] **Step 2: Run the full host-side suite**

Run:

```bash
python3 tools/test_text_raster.py
python3 tools/test_weather_icons.py
python3 tools/test_weather_palette.py
python3 tools/test_weather_dashboard.py
python3 tools/test_weather_preview.py
```

Expected: all tests pass.

- [ ] **Step 3: Verify the browser preview**

Serve the repository on an available localhost port and test this flow:

`weather-preview/ loads -> live or sample data renders -> native panel remains 768x552 -> desktop and mobile widths show the complete panel without overlap.`

Check title/URL, meaningful DOM, no framework overlay, no relevant console errors, screenshot evidence, seven forecast columns, hourly rain icons, right-aligned update, and the final forecast boundary aligned to the main frame.

- [ ] **Step 4: Compile the ESP8266 firmware**

Run:

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 \
  --output-dir /tmp/nova14-weather-eight-hour-build \
  SE0398NZ07A0_Shenzhen_Weather
```

Expected: successful compile with flash/RAM usage reported.

- [ ] **Step 5: Resolve the connected ESP8266 port safely**

Run `arduino-cli board list`, then probe only listed serial candidates with the ESP8266 core's bundled esptool `chip_id`. If exactly one reports ESP8266, use it. If none report ESP8266, report upload blocked. If multiple report ESP8266, ask which physical device drives the display.

- [ ] **Step 6: Upload the compiled build**

Run:

```bash
arduino-cli upload --fqbn esp8266:esp8266:nodemcuv2 \
  --input-dir /tmp/nova14-weather-eight-hour-build \
  -p <verified-esp8266-port> \
  SE0398NZ07A0_Shenzhen_Weather
```

Expected: upload and board reset succeed.

- [ ] **Step 7: Inspect startup output when available**

Read the verified port at 115200 baud and look for the firmware banner, Wi-Fi connection, `Weather values` with `hourly 8`, successful update, and display refresh. Report upload success separately from physical screen confirmation if serial access or direct panel observation is unavailable.

- [ ] **Step 8: Commit final documentation only**

```bash
git add README.md
git commit -m "docs: describe eight-hour seven-day weather dashboard"
```
