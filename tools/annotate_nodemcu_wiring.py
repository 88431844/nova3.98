#!/usr/bin/env python3

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps


EXPECTED_SIZE = (826, 1426)
CANVAS_SIZE = (1600, 1720)
PHOTO_ORIGIN = (387, 120)
FONT_PATH = "/System/Library/Fonts/STHeiti Medium.ttc"


def font(size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(FONT_PATH, size=size)


def arrow(draw: ImageDraw.ImageDraw, start, end, color, width=7, head=18):
    draw.line([start, end], fill="white", width=width + 6)
    draw.line([start, end], fill=color, width=width)

    angle = math.atan2(end[1] - start[1], end[0] - start[0])
    left = (
        end[0] - head * math.cos(angle - math.pi / 6),
        end[1] - head * math.sin(angle - math.pi / 6),
    )
    right = (
        end[0] - head * math.cos(angle + math.pi / 6),
        end[1] - head * math.sin(angle + math.pi / 6),
    )
    draw.polygon([end, left, right], fill=color)


def pin_circle(draw: ImageDraw.ImageDraw, point, color):
    x, y = point
    draw.ellipse((x - 21, y - 21, x + 21, y + 21), outline="white", width=11)
    draw.ellipse((x - 21, y - 21, x + 21, y + 21), outline=color, width=7)


def label_box(draw, box, color, title, subtitle, arrow_start, pin):
    draw.rounded_rectangle(box, radius=14, fill="#FFFFFF", outline=color, width=4)
    x1, y1, _, _ = box
    draw.ellipse((x1 + 16, y1 + 20, x1 + 42, y1 + 46), fill=color)
    draw.text((x1 + 55, y1 + 8), title, font=font(27), fill="#111827")
    draw.text((x1 + 55, y1 + 42), subtitle, font=font(21), fill="#4B5563")
    arrow(draw, arrow_start, pin, color)
    pin_circle(draw, pin, color)


def translated(point):
    return point[0] + PHOTO_ORIGIN[0], point[1] + PHOTO_ORIGIN[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    source = ImageOps.exif_transpose(Image.open(args.input)).convert("RGB")
    if source.size != EXPECTED_SIZE:
        raise ValueError(f"Expected {EXPECTED_SIZE}, got {source.size}")

    canvas = Image.new("RGB", CANVAS_SIZE, "#EEF2F6")
    canvas.paste(source, PHOTO_ORIGIN)
    draw = ImageDraw.Draw(canvas)

    x0, y0 = PHOTO_ORIGIN
    draw.rounded_rectangle(
        (x0 - 5, y0 - 5, x0 + source.width + 5, y0 + source.height + 5),
        radius=10,
        outline="#9CA3AF",
        width=4,
    )

    title = "SE0398NZ07A0 A0 屏幕 → NodeMCU ESP8266 接线"
    title_box = draw.textbbox((0, 0), title, font=font(39))
    title_width = title_box[2] - title_box[0]
    draw.text(((CANVAS_SIZE[0] - title_width) / 2, 35), title, font=font(39), fill="#111827")

    # Pin centers measured from the supplied 826x1426 photograph.
    pins = {
        "D0": translated((684, 288)),
        "D1": translated((684, 347)),
        "D2": translated((684, 405)),
        "D5": translated((684, 691)),
        "D6": translated((684, 750)),
        "D7": translated((684, 807)),
        "GND": translated((135, 808)),
        "3V3": translated((135, 866)),
    }

    right_labels = [
        ((1130, 366, 1575, 438), "#E83E8C", "屏幕 RST", "焊到 D0 / GPIO16", "D0"),
        ((1130, 443, 1575, 515), "#FF8C00", "屏幕 CS", "焊到 D1 / GPIO5", "D1"),
        ((1130, 520, 1575, 592), "#D6A900", "屏幕 DC", "焊到 D2 / GPIO4", "D2"),
        ((1130, 776, 1575, 848), "#2979FF", "屏幕 CLK/SCK", "焊到 D5 / GPIO14", "D5"),
        ((1130, 853, 1575, 925), "#8E44AD", "屏幕 BUSY", "焊到 D6 / GPIO12（输入）", "D6"),
        ((1130, 930, 1575, 1002), "#00A86B", "屏幕 SDA/MOSI", "焊到 D7 / GPIO13", "D7"),
    ]

    for box, color, title_text, subtitle, pin_name in right_labels:
        label_box(
            draw,
            box,
            color,
            title_text,
            subtitle,
            (box[0], (box[1] + box[3]) // 2),
            pins[pin_name],
        )

    left_labels = [
        ((25, 886, 465, 958), "#343A40", "屏幕 GND", "焊到左侧 GND", "GND"),
        ((25, 973, 465, 1045), "#D32F2F", "屏幕 3.3V", "焊到左侧 3V3", "3V3"),
    ]

    for box, color, title_text, subtitle, pin_name in left_labels:
        label_box(
            draw,
            box,
            color,
            title_text,
            subtitle,
            (box[2], (box[1] + box[3]) // 2),
            pins[pin_name],
        )

    safety_box = (90, 1570, 1510, 1688)
    draw.rounded_rectangle(safety_box, radius=18, fill="#FFF4E5", outline="#D97706", width=4)
    warning = "只允许 3.3V：禁止把屏幕接到 VIN、VU、5V 或 USB 5V"
    detail = "GND 最先接，3.3V 最后接；SDA 是 SPI MOSI；D3 / D4 / D8 不使用"
    warning_width = draw.textbbox((0, 0), warning, font=font(29))[2]
    detail_width = draw.textbbox((0, 0), detail, font=font(24))[2]
    draw.text(((CANVAS_SIZE[0] - warning_width) / 2, 1585), warning, font=font(29), fill="#9A3412")
    draw.text(((CANVAS_SIZE[0] - detail_width) / 2, 1635), detail, font=font(24), fill="#4B5563")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(args.output, format="PNG", optimize=True)


if __name__ == "__main__":
    main()
