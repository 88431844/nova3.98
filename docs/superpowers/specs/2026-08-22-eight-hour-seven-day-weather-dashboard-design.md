# Eight-Hour And Seven-Day Weather Dashboard Design

## Goal

Update the SE0398NZ07A0 Shenzhen weather dashboard and add a permanent browser preview that uses the same 768x552 coordinates. The approved result must:

- show eight hourly temperature points instead of twelve;
- show a weather icon below every hourly temperature so rain is visible at a glance;
- enlarge the hourly temperature and time labels;
- show the next seven days instead of three forecast cards;
- label those days with `周日` through `周六` instead of relative day names;
- use degree marks (`°`) instead of `C` for every temperature;
- retain the free, keyless Open-Meteo API;
- use the approved Iconfont `天气库 · 方案一` weather icon family;
- compile and upload the result to the connected ESP8266.

The accepted visual reference is `.superpowers/brainstorm/35199-1787381225/content/full-weather-layout-polish-v10.html`.

## Display Geometry

The panel remains 768x552 with the verified logical palette `BLACK=0`, `WHITE=1`, `YELLOW=2`, and `RED=3`. The weather page remains unmirrored.

### Header

- Keep the red rule at `y=0..5`.
- Remove the `深圳天气` title.
- Draw the IP address from `x=22` on the left.
- Center the current `YYYY-MM-DD` date in the header.
- Draw `更新 HH:MM` as one compact group ending at `x=746`; do not repeat `MM-DD` in the update value.
- Draw IP/date ASCII at `y=21` and the `更新` CJK label at `y=20`, which gives the three groups the same visual baseline.

### Current Weather

- Move the shared current-weather/chart frame to `x=16..752`, `y=52..310` so it visually connects to the header.
- Keep the current-weather/chart divider at `x=316`.
- Draw the current icon at `x=36`, `y=80` in an 84x84 target box.
- Draw the current temperature at `x=145`, `y=78` and the condition at `x=145`, `y=142`.
- Keep the separator across the current-weather column at `y=210`.
- Draw `体感` and `湿度` at `y=258`, with their values at `y=266`.
- Render current and apparent temperatures with a degree mark and no `C`.

### Eight-Hour Chart

- Keep the chart frame at `x=316..752`, `y=52..310`.
- Change the title to `近8小时天气` and draw it at `x=336`, `y=56`.
- Use eight consecutive hourly records starting at the first hourly timestamp greater than or equal to `current.time`.
- Place the eight samples across `x=340..730`.
- Draw integer temperature labels with degree marks at `y=94`, using the enlarged scale-2 appearance.
- Draw a 28x28 weather icon below each temperature at `y=120`, centered on its sample x-coordinate.
- Use yellow for clear/cloud conditions and red for rain, snow, and thunder conditions.
- Draw a light guide from each icon band to its yellow point, then connect the points with the red line.
- Draw all eight full `HH:MM` labels at scale 2. Alternate them between `y=264` and `y=288` and clamp the first/last label origins inside the chart frame so they do not overlap or clip.

### Seven-Day Forecast

- Request eight daily records, skip source day 0 (today), and display source days 1 through 7.
- Use one forecast frame spanning `x=16..752`, `y=318..540` with seven equal columns.
- Compute every boundary with integer division from the shared 736-pixel width. The final column's right edge must be exactly `x=752`, aligned with the current-weather frame.
- Calculate the weekday from each ISO date and display `周日`, `周一`, `周二`, `周三`, `周四`, `周五`, or `周六` at the top of each column.
- Keep the `MM-DD` date, weather icon, high/low temperature, and precipitation probability in each column.
- Use the compact approved order: weekday, date, 64x64 icon, `high°/low°`, and `降水 probability`.
- Weather condition text is omitted in the seven-column layout; the icon carries that information.

## Weather Data

Continue using the free, keyless Open-Meteo forecast endpoint for Shenzhen coordinates `22.5431,114.0579` and timezone `Asia/Shanghai`.

The request must include:

- `forecast_days=8`;
- current temperature, humidity, apparent temperature, and weather code;
- hourly `temperature_2m` and `weather_code`;
- daily maximum/minimum temperature, maximum precipitation probability, weather code, sunrise, and sunset.

`WeatherData` changes from 12 hourly entries to 8 and from 3 daily entries to 7. Hourly temperatures, hourly weather codes, and hourly times must use the same source index. All daily arrays must use the same `sourceDay = day + 1` index.

Reject a response unless current values, all eight hourly samples, and all seven future daily samples are present and valid. A failed refresh must preserve the last valid dashboard data.

## Weekdays And Degree Marks

Add CJK glyph coverage for `近`, `小`, `时`, `周`, `一`, `二`, `三`, `四`, `五`, `六`, and `日` when missing.

Do not pass the UTF-8 degree character through the current byte-oriented ASCII renderer. Add a temperature drawing helper that renders the numeric part with the existing 5x7 font and draws a small superscript circle at the correct scale. The helper must support current, apparent, hourly, and high/low temperature layouts without changing their surrounding coordinates.

## Icon Assets

Replace the preview's temporary weather shapes and the firmware's active weather masks with the coherent Iconfont `天气库 · 方案一` family. Source identifiers selected during visual review include:

- `3010907`: sunny;
- `3010910`: cloudy;
- `3010909`: overcast;
- `3010912`: light rain;
- `3010915`: moderate rain;
- `3010914`: heavy rain;
- `3010916`: rainstorm;
- `3010924`: fog;
- `3010937`: thunder.

Store source path data in the existing generator input and generate a single high-resolution monochrome mask per condition. Add a row renderer that scales the mask to the approved current, hourly, and seven-day sizes without storing three duplicate mask families. Scaling must preserve aspect ratio and center the icon within its target box.

## Permanent Web Preview

Add a standalone browser preview under `weather-preview/`. It must:

- render a native 768x552 dashboard and scale it responsively without changing internal coordinates;
- use the same sample positions, colors, labels, weekdays, and Iconfont paths as the firmware;
- fetch live Open-Meteo data when available;
- fall back to deterministic sample data when the request fails, while visibly indicating sample data outside the simulated panel;
- require no build step or new package dependency.

The preview is a coordinate simulator, not a separate responsive dashboard design. Responsive behavior only scales the complete 768x552 panel to fit the browser viewport.

## Error Handling

- Preserve the last valid weather response after HTTP, TLS, JSON, or validation failures.
- Continue showing `IP NO WIFI` when Wi-Fi is unavailable.
- The web preview must catch request failures and render sample data rather than a blank canvas.
- Unknown weather codes use the cloudy icon and black condition text.

## Verification

Host-side tests must prove:

- eight hourly entries and aligned hourly weather-code parsing;
- eight requested forecast days and daily source indices 1 through 7;
- weekday calculation for known dates, including the approved `2026-08-23` Sunday example;
- header removal and the compact right-aligned update time;
- the `y=52` main frame and top-aligned chart title;
- enlarged temperature/time labels, alternating time rows, and degree drawing helper;
- seven equal forecast columns ending at `x=752`;
- required CJK glyph coverage and Iconfont asset identifiers.

Run all existing weather palette, text-raster, icon, and dashboard tests. Add or update focused tests before implementation changes.

Verify the permanent preview in a browser at desktop and mobile widths. Check page identity, meaningful content, console health, the live-data or sample-data state, panel edge alignment, and absence of overlap or clipping.

Compile with `arduino-cli` for `esp8266:esp8266:nodemcuv2`. If an ESP8266 serial port is present, upload the build, inspect serial output at 115200 baud, and distinguish successful upload from physical panel verification if the refreshed display cannot be observed directly.

## Scope

Modify only the Shenzhen weather sketch, its generated font/icon assets and generators, focused host tests, the permanent web preview, and concise README documentation. Preserve the stock page, panel row mapping, Wi-Fi/button behavior, and unrelated working-tree changes.
