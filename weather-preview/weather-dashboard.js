"use strict";

const HOURLY_POINTS = 8;
const FORECAST_DAYS = 7;
const PANEL_WIDTH = 768;
const PANEL_HEIGHT = 552;
const CURRENT_TEMPERATURE_SIZE = 68;
const CURRENT_TEMPERATURE_X = 135;
const CURRENT_TEMPERATURE_Y = 66;
const CURRENT_CONDITION_Y = 150;
const HOURLY_TEMPERATURE_Y = 62;
const HOURLY_ICON_Y = 88;
const COLORS = {
  ink: "#111111",
  paper: "#fffef8",
  red: "#d52218",
  yellow: "#efb900",
  guide: "#b9b5a7",
};

const SAMPLE_WEATHER = {
  ip: "IP 192.168.1.86",
  date: "2026-08-22",
  updated: "09:10",
  temperature: 31,
  humidity: 68,
  sunrise: "06:08",
  sunset: "18:47",
  precipitationProbability: 30,
  currentCode: 0,
  hourly: [
    { time: "09:00", temperature: 27, code: 0 },
    { time: "10:00", temperature: 28, code: 2 },
    { time: "11:00", temperature: 29, code: 61 },
    { time: "12:00", temperature: 29, code: 63 },
    { time: "13:00", temperature: 30, code: 3 },
    { time: "14:00", temperature: 31, code: 0 },
    { time: "15:00", temperature: 31, code: 2 },
    { time: "16:00", temperature: 32, code: 80 },
  ],
  daily: [
    { date: "2026-08-23", high: 33, low: 27, rain: 30, code: 2 },
    { date: "2026-08-24", high: 32, low: 26, rain: 65, code: 61 },
    { date: "2026-08-25", high: 34, low: 27, rain: 10, code: 0 },
    { date: "2026-08-26", high: 33, low: 27, rain: 25, code: 3 },
    { date: "2026-08-27", high: 31, low: 26, rain: 70, code: 63 },
    { date: "2026-08-28", high: 34, low: 27, rain: 15, code: 0 },
    { date: "2026-08-29", high: 32, low: 26, rain: 55, code: 80 },
  ],
};

const canvas = document.querySelector("#weather-panel");
const ctx = canvas.getContext("2d");
const iconLayer = document.querySelector("#icon-layer");
const status = document.querySelector("#data-status");

function mono(text, x, y, size = 16, weight = 800, color = COLORS.ink, align = "left") {
  ctx.fillStyle = color;
  ctx.font = `${weight} ${size}px ui-monospace, SFMono-Regular, Consolas, monospace`;
  ctx.textAlign = align;
  ctx.textBaseline = "top";
  ctx.fillText(text, x, y);
}

function label(text, x, y, size = 16, weight = 800, color = COLORS.ink, align = "left") {
  ctx.fillStyle = color;
  ctx.font = `${weight} ${size}px "PingFang SC", "Microsoft YaHei", sans-serif`;
  ctx.textAlign = align;
  ctx.textBaseline = "top";
  ctx.fillText(text, x, y);
}

function box(left, top, right, bottom) {
  ctx.strokeStyle = COLORS.ink;
  ctx.lineWidth = 2;
  ctx.strokeRect(left + 0.5, top + 0.5, right - left, bottom - top);
}

function weatherText(code) {
  if (code === 0) return "晴";
  if (code <= 2) return "多云";
  if (code === 3) return "阴";
  if (code === 45 || code === 48) return "雾";
  if (code >= 71 && code <= 77) return "雪";
  if (code >= 95) return "雷雨";
  if (code === 82 || (code >= 65 && code <= 67)) return "大雨";
  return code <= 55 ? "小雨" : "中雨";
}

function weekdayFromIsoDate(date) {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(date);
  if (!match) return 0;
  let year = Number(match[1]);
  const month = Number(match[2]);
  const day = Number(match[3]);
  const offsets = [0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4];
  if (month < 3) year -= 1;
  return (year + Math.floor(year / 4) - Math.floor(year / 100) +
    Math.floor(year / 400) + offsets[month - 1] + day) % 7;
}

function weekdayLabel(date) {
  return ["周日", "周一", "周二", "周三", "周四", "周五", "周六"][
    weekdayFromIsoDate(date)
  ];
}

function iconName(code) {
  if (code === 0) return "sun";
  if (code <= 3 || code === 45 || code === 48 || (code >= 71 && code <= 77)) {
    return "cloud";
  }
  return "rain";
}

function iconColor(code) {
  return iconName(code) === "rain" ? COLORS.red : COLORS.yellow;
}

function addIcon(code, x, y, width, height) {
  const use = document.createElementNS("http://www.w3.org/2000/svg", "use");
  use.setAttribute("href", `#if-${iconName(code)}`);
  use.setAttribute("x", x);
  use.setAttribute("y", y);
  use.setAttribute("width", width);
  use.setAttribute("height", height);
  use.setAttribute("fill", iconColor(code));
  iconLayer.append(use);
}

function chartY(value, minimum, maximum) {
  if (maximum - minimum < 0.1) return (158 + 247) / 2;
  return 247 - ((value - minimum) / (maximum - minimum)) * (247 - 158);
}

function renderDashboard(data) {
  ctx.clearRect(0, 0, PANEL_WIDTH, PANEL_HEIGHT);
  iconLayer.replaceChildren();
  ctx.fillStyle = COLORS.paper;
  ctx.fillRect(0, 0, PANEL_WIDTH, PANEL_HEIGHT);
  ctx.fillStyle = COLORS.red;
  ctx.fillRect(0, 0, PANEL_WIDTH, 6);

  mono(data.ip, 22, 21, 16, 800);
  mono(data.date, PANEL_WIDTH / 2, 21, 16, 800, COLORS.ink, "center");
  label("更新", 640, 20, 16, 800);
  mono(data.updated, 746, 21, 16, 800, COLORS.ink, "right");

  box(16, 52, 752, 310);
  box(316, 52, 752, 310);
  addIcon(data.currentCode, 36, 80, 84, 84);
  mono(
    `${Math.round(data.temperature)}°`,
    CURRENT_TEMPERATURE_X,
    CURRENT_TEMPERATURE_Y,
    CURRENT_TEMPERATURE_SIZE,
    900,
  );
  label(weatherText(data.currentCode), 145, CURRENT_CONDITION_Y, 31, 800);
  ctx.fillStyle = COLORS.ink;
  ctx.fillRect(16, 202, 300, 1);
  ctx.fillRect(16, 256, 300, 1);
  ctx.fillRect(166, 202, 1, 108);
  label("日出", 28, 218, 22, 900);
  mono(data.sunrise, 80, 220, 20, 800);
  label("日落", 178, 218, 22, 900);
  mono(data.sunset, 230, 220, 20, 800);
  label("降水概率", 24, 264, 21, 900);
  mono(`${Math.round(data.precipitationProbability)}%`, 118, 266, 20, 800);
  label("湿度", 178, 264, 22, 900);
  mono(`${Math.round(data.humidity)}%`, 230, 266, 20, 800);

  const hourly = data.hourly.slice(0, HOURLY_POINTS);
  const temperatures = hourly.map((entry) => entry.temperature);
  const minimum = Math.min(...temperatures);
  const maximum = Math.max(...temperatures);
  const points = hourly.map((entry, index) => {
    const x = 340 + (index * (730 - 340)) / (HOURLY_POINTS - 1);
    return { ...entry, x, y: chartY(entry.temperature, minimum, maximum) };
  });

  ctx.setLineDash([3, 3]);
  ctx.strokeStyle = COLORS.guide;
  ctx.lineWidth = 1;
  points.forEach((point) => {
    mono(
      `${Math.round(point.temperature)}°`,
      point.x,
      HOURLY_TEMPERATURE_Y,
      20,
      900,
      COLORS.ink,
      "center",
    );
    addIcon(point.code, point.x - 14, HOURLY_ICON_Y, 28, 28);
    ctx.beginPath();
    ctx.moveTo(point.x, 119);
    ctx.lineTo(point.x, Math.max(119, point.y - 7));
    ctx.stroke();
  });
  ctx.setLineDash([]);
  ctx.strokeStyle = COLORS.red;
  ctx.lineWidth = 3;
  ctx.beginPath();
  points.forEach((point, index) => {
    if (index === 0) ctx.moveTo(point.x, point.y);
    else ctx.lineTo(point.x, point.y);
  });
  ctx.stroke();
  points.forEach((point, index) => {
    ctx.fillStyle = COLORS.yellow;
    ctx.strokeStyle = COLORS.ink;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(point.x, point.y, 5, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
    mono(point.time.slice(0, 2), point.x, 282, 16, 900, COLORS.ink, "center");
  });

  data.daily.slice(0, FORECAST_DAYS).forEach((day, index) => {
    const left = 16 + Math.floor((index * 736) / FORECAST_DAYS);
    const right = 16 + Math.floor(((index + 1) * 736) / FORECAST_DAYS);
    const center = (left + right) / 2;
    box(left, 318, right, 540);
    label(weekdayLabel(day.date), center, 326, 26, 900, COLORS.ink, "center");
    mono(day.date.slice(5), center, 362, 14, 800, COLORS.ink, "center");
    addIcon(day.code, center - 32, 390, 64, 64);
    mono(`${Math.round(day.high)}°/${Math.round(day.low)}°`, center, 472, 16, 900, COLORS.ink, "center");
    label(`降水 ${Math.round(day.rain)}%`, center, 508, 14, 900, COLORS.ink, "center");
  });
}

function mapOpenMeteo(payload) {
  const current = payload.current;
  const hourly = payload.hourly;
  const daily = payload.daily;
  if (!current || !hourly || !daily || typeof current.time !== "string") {
    throw new Error("天气接口字段不完整");
  }
  if (!Array.isArray(daily.sunrise) || !Array.isArray(daily.sunset)) {
    throw new Error("天气接口缺少日出日落");
  }
  const firstHour = hourly.time.findIndex((time) => time >= current.time);
  if (firstHour < 0) throw new Error("天气接口缺少当前小时");
  const alignedHourly = Array.from({ length: HOURLY_POINTS }, (_, index) => {
    const source = firstHour + index;
    return {
      time: hourly.time[source]?.slice(11, 16),
      temperature: Number(hourly.temperature_2m[source]),
      code: Number(hourly.weather_code[source]),
    };
  });
  const alignedDaily = Array.from({ length: FORECAST_DAYS }, (_, index) => {
    const source = index + 1;
    return {
      date: daily.time[source],
      high: Number(daily.temperature_2m_max[source]),
      low: Number(daily.temperature_2m_min[source]),
      rain: Number(daily.precipitation_probability_max[source]),
      code: Number(daily.weather_code[source]),
    };
  });
  const validHourly = alignedHourly.every((entry) =>
    entry.time && Number.isFinite(entry.temperature) && Number.isFinite(entry.code)
  );
  const validDaily = alignedDaily.every((entry) =>
    entry.date && Number.isFinite(entry.high) && Number.isFinite(entry.low) &&
    Number.isFinite(entry.rain) && Number.isFinite(entry.code)
  );
  const precipitationProbability = Number(daily.precipitation_probability_max[0]);
  if (!validHourly || !validDaily || !Number.isFinite(precipitationProbability)) {
    throw new Error("天气接口记录不足");
  }
  return {
    ip: SAMPLE_WEATHER.ip,
    date: current.time.slice(0, 10),
    updated: current.time.slice(11, 16),
    temperature: Number(current.temperature_2m),
    humidity: Number(current.relative_humidity_2m),
    sunrise: daily.sunrise[0].slice(11, 16),
    sunset: daily.sunset[0].slice(11, 16),
    precipitationProbability,
    currentCode: Number(current.weather_code),
    hourly: alignedHourly,
    daily: alignedDaily,
  };
}

async function loadWeather() {
  const endpoint = "https://api.open-meteo.com/v1/forecast?latitude=22.5431&longitude=114.0579&timezone=Asia%2FShanghai&forecast_days=8&current=temperature_2m,relative_humidity_2m,weather_code&hourly=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code,sunrise,sunset";
  const controller = new AbortController();
  const requestTimeout = setTimeout(() => controller.abort(), 8000);
  try {
    const response = await fetch(endpoint, {
      cache: "no-store",
      signal: controller.signal,
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = mapOpenMeteo(await response.json());
    renderDashboard(data);
    status.textContent = `实时数据 · ${data.updated} 更新`;
  } catch (error) {
    console.warn("Open-Meteo unavailable; showing sample data", error);
    renderDashboard(SAMPLE_WEATHER);
    status.textContent = "示例数据 · 实时接口暂不可用";
  } finally {
    clearTimeout(requestTimeout);
  }
}

renderDashboard(SAMPLE_WEATHER);
loadWeather();
