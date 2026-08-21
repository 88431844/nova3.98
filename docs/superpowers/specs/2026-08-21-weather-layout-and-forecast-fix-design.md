# Weather Layout And Forecast Fix Design

## Goal

Correct the Shenzhen weather page shown on the SE0398NZ07A0 panel and improve its readability:

- render a true white background;
- show tomorrow, the day after tomorrow, and three days from today;
- enlarge the three forecast labels and hourly-chart text;
- show the device IP and weather update time between the title and date;
- widen the hourly chart without reducing the three forecast cards;
- keep apparent temperature and humidity on a dedicated, unobstructed row.

## Diagnosed Causes

- The panel's verified native 2-bit palette is `BLACK=0`, `WHITE=1`, `YELLOW=2`, `RED=3`. The working weather sketch currently declares `YELLOW=1`, `RED=2`, and `WHITE=3`, so a row filled as white is physically rendered red.
- Open-Meteo currently receives `forecast_days=3`, and the sketch copies daily entries 0 through 2. Those entries mean today, tomorrow, and the day after tomorrow, which does not match the requested three future days.
- The chart occupies only x=450 through x=752, and its title and endpoint times use ASCII scale 1.
- The current-weather content and its detail text share limited vertical space, making the apparent-temperature row visually crowded.

## Data Design

- Request four daily records with `forecast_days=4` and retain the existing Shenzhen coordinates and `Asia/Shanghai` timezone.
- Copy daily source entries 1, 2, and 3 into the three forecast arrays. Keep all high, low, rain, weather-code, date, sunrise, and sunset fields aligned to the same source index.
- Store the current local date separately from the forecast dates. Derive it from `current.time`, which is also the source for the displayed weather update timestamp.
- Label the forecast cards `明天`, `后天`, and `大后天`. Add the missing `大` glyph to the generated 16x16 CJK font.
- Keep the current 12 hourly points beginning at the first hourly timestamp greater than or equal to `current.time`.

## Rendering Design

### Palette And Background

- Restore the verified enum order: `BLACK=0`, `WHITE=1`, `YELLOW=2`, `RED=3`.
- Continue filling every weather row with `WHITE`; after the palette correction this produces the requested physical white background.
- Keep black as the primary text color, yellow for ordinary weather icons and chart points, and red for the chart line and severe/rain accents.

### Header

- Keep the top red rule and a white header background.
- Render four left-to-right groups: `深圳天气` at x=22, device IP at x=174, `更新` at x=398 with its `MM-DD HH:MM` value at x=438, and the current `YYYY-MM-DD` date at x=620.
- Use black text. Render the title, IP, update value, and date at scale 2; render the `更新` label at CJK scale 1. These positions leave separation even when the IP text is `IP 255.255.255.255`.

### Current Weather And Chart

- Keep the shared outer area at x=16..752 and y=76..310.
- Give current weather x=16..316 (300 px) and the chart x=316..752 (436 px).
- Move the temperature and condition left to fit the narrower current-weather column.
- Draw apparent temperature and humidity on a dedicated lower row separated from the icon/temperature block. Do not allow either label or value to enter the chart region.
- Expand the chart plot to x=340..730 while retaining its current vertical range.
- Increase the chart title and both endpoint times from ASCII scale 1 to scale 2. Keep the line and point geometry independent of label size.

### Forecast Cards

- Keep three equal-width cards across x=16..752.
- Put the large day label at y=328 and the `MM-DD` date on its own line at y=366.
- Use CJK scale 2 for `明天`, `后天`, and `大后天`.
- Draw each 96x78 icon at y=390, high/low temperatures at y=474 with ASCII scale 2, and rain probability at y=510..526 with its numeric value at ASCII scale 2. These fixed bands must not overlap.

## Error Handling

- Reject a weather response unless current conditions and all four daily records required for source indices 1..3 are present and valid.
- Preserve the last valid weather data when a refresh fails.
- Continue displaying `IP NO WIFI` when Wi-Fi connection fails.

## Verification

- Add a host-side regression test that proves the physical color codes map white to 1 and red to 3.
- Add a host-side regression test that proves the request uses four forecast days and the three displayed entries use source indices 1..3 with the requested labels.
- Add layout checks for the four header groups, widened chart constants, scale-2 chart labels, dedicated apparent-temperature/humidity row, and non-overlapping forecast text coordinates.
- Run all existing weather text, icon, and palette tests.
- Compile the weather sketch with `arduino-cli` for `esp8266:esp8266:nodemcuv2`.
- If a serial device is connected, upload the build and inspect the refreshed panel; otherwise report that hardware verification remains pending.

## Scope

Modify only the Shenzhen weather sketch, its CJK font asset or generator input as required for `大`, focused host tests, and concise README notes. Preserve the existing stock page, panel row mapping, icon set, Wi-Fi behavior, and unrelated working-tree changes.
