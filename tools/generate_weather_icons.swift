import CoreGraphics
import Foundation

let arguments = CommandLine.arguments
guard arguments.count == 3 else {
  fputs("usage: generate_weather_icons.swift input.json output.h\n", stderr)
  exit(2)
}

struct PathRecord: Codable {
  let d: String
}

struct IconRecord: Codable {
  let viewBox: String
  let path: [PathRecord]
}

let sourceURL = URL(fileURLWithPath: arguments[1])
let outputURL = URL(fileURLWithPath: arguments[2])
let records = try JSONDecoder().decode(
  [String: IconRecord].self,
  from: Data(contentsOf: sourceURL)
)

let canvasWidth = 96
let canvasHeight = 96
let maskBytes = (canvasWidth / 8) * canvasHeight

struct IconSpec {
  let symbol: String
  let sourceName: String
}

let specs = [
  IconSpec(symbol: "Sunny", sourceName: "晴天"),
  IconSpec(symbol: "PartlyCloudy", sourceName: "多云"),
  IconSpec(symbol: "Cloudy", sourceName: "多云"),
  IconSpec(symbol: "Rain", sourceName: "小雨"),
  IconSpec(symbol: "HeavyRain", sourceName: "大雨"),
  IconSpec(symbol: "Snow", sourceName: "大雪兼容"),
  IconSpec(symbol: "Storm", sourceName: "打雷"),
  IconSpec(symbol: "Fog", sourceName: "雾"),
]

enum ParseError: Error {
  case malformedPath(String)
  case unsupportedCommand(Character)
}

func tokenize(_ source: String) -> [String] {
  var result: [String] = []
  var number = ""
  func flush() {
    if !number.isEmpty {
      result.append(number)
      number = ""
    }
  }

  for scalar in source.unicodeScalars {
    let character = Character(String(scalar))
    if character.isLetter {
      flush()
      result.append(String(character))
    } else if character == "-" || character == "." || character.isNumber ||
              character == "e" || character == "E" || character == "+" {
      if (character == "-" || character == "+") &&
          !number.isEmpty && !number.hasSuffix("e") && !number.hasSuffix("E") {
        flush()
      }
      number.append(character)
    } else {
      flush()
    }
  }
  flush()
  return result
}

func number(_ tokens: [String], _ index: inout Int) throws -> CGFloat {
  guard index < tokens.count, let value = Double(tokens[index]) else {
    throw ParseError.malformedPath("expected number")
  }
  index += 1
  return CGFloat(value)
}

func pathFromSVG(_ source: String) throws -> CGPath {
  let tokens = tokenize(source)
  let path = CGMutablePath()
  var index = 0
  var command: Character = "M"
  var current = CGPoint.zero
  var subpathStart = CGPoint.zero
  var lastCubicControl: CGPoint?
  var lastQuadControl: CGPoint?

  while index < tokens.count {
    if tokens[index].count == 1,
       let candidate = tokens[index].first,
       candidate.isLetter {
      command = candidate
      index += 1
    }

    let relative = command.isLowercase
    let upper = Character(command.uppercased())
    func point(_ x: CGFloat, _ y: CGFloat) -> CGPoint {
      relative ? CGPoint(x: current.x + x, y: current.y + y) : CGPoint(x: x, y: y)
    }
    func control(_ x: CGFloat, _ y: CGFloat) -> CGPoint {
      point(x, y)
    }

    switch upper {
    case "M":
      let next = point(try number(tokens, &index), try number(tokens, &index))
      path.move(to: next)
      current = next
      subpathStart = next
      lastCubicControl = nil
      lastQuadControl = nil
      command = relative ? "l" : "L"
    case "L":
      let next = point(try number(tokens, &index), try number(tokens, &index))
      path.addLine(to: next)
      current = next
      lastCubicControl = nil
      lastQuadControl = nil
    case "H":
      let x = try number(tokens, &index)
      let next = relative ? CGPoint(x: current.x + x, y: current.y) : CGPoint(x: x, y: current.y)
      path.addLine(to: next)
      current = next
      lastCubicControl = nil
      lastQuadControl = nil
    case "V":
      let y = try number(tokens, &index)
      let next = relative ? CGPoint(x: current.x, y: current.y + y) : CGPoint(x: current.x, y: y)
      path.addLine(to: next)
      current = next
      lastCubicControl = nil
      lastQuadControl = nil
    case "C":
      let c1 = control(try number(tokens, &index), try number(tokens, &index))
      let c2 = control(try number(tokens, &index), try number(tokens, &index))
      let next = point(try number(tokens, &index), try number(tokens, &index))
      path.addCurve(to: next, control1: c1, control2: c2)
      current = next
      lastCubicControl = c2
      lastQuadControl = nil
    case "S":
      let c1 = lastCubicControl.map { CGPoint(x: 2 * current.x - $0.x, y: 2 * current.y - $0.y) } ?? current
      let c2 = control(try number(tokens, &index), try number(tokens, &index))
      let next = point(try number(tokens, &index), try number(tokens, &index))
      path.addCurve(to: next, control1: c1, control2: c2)
      current = next
      lastCubicControl = c2
      lastQuadControl = nil
    case "Q":
      let controlPoint = control(try number(tokens, &index), try number(tokens, &index))
      let next = point(try number(tokens, &index), try number(tokens, &index))
      path.addQuadCurve(to: next, control: controlPoint)
      current = next
      lastQuadControl = controlPoint
      lastCubicControl = nil
    case "T":
      let controlPoint = lastQuadControl.map { CGPoint(x: 2 * current.x - $0.x, y: 2 * current.y - $0.y) } ?? current
      let next = point(try number(tokens, &index), try number(tokens, &index))
      path.addQuadCurve(to: next, control: controlPoint)
      current = next
      lastQuadControl = controlPoint
      lastCubicControl = nil
    case "Z":
      path.closeSubpath()
      current = subpathStart
      lastCubicControl = nil
      lastQuadControl = nil
    default:
      throw ParseError.unsupportedCommand(command)
    }
  }
  return path
}

func viewBoxSize(_ value: String) throws -> CGSize {
  let parts = value.split(separator: " ").compactMap { Double($0) }
  guard parts.count == 4, parts[2] > 0, parts[3] > 0 else {
    throw ParseError.malformedPath("invalid viewBox: \(value)")
  }
  return CGSize(width: parts[2], height: parts[3])
}

func rasterize(_ record: IconRecord) throws -> [UInt8] {
  let viewBox = try viewBoxSize(record.viewBox)
  let scale = min(CGFloat(canvasWidth - 8) / viewBox.width,
                  CGFloat(canvasHeight - 8) / viewBox.height)
  let offset = CGPoint(x: (CGFloat(canvasWidth) - viewBox.width * scale) / 2,
                       y: (CGFloat(canvasHeight) - viewBox.height * scale) / 2)
  var pixels = [UInt8](repeating: 255, count: canvasWidth * canvasHeight)
  guard let context = CGContext(data: &pixels, width: canvasWidth, height: canvasHeight,
                                bitsPerComponent: 8, bytesPerRow: canvasWidth,
                                space: CGColorSpaceCreateDeviceGray(), bitmapInfo: 0) else {
    fatalError("could not create raster context")
  }
  context.setFillColor(gray: 0, alpha: 1)
  context.translateBy(x: 0, y: CGFloat(canvasHeight))
  context.scaleBy(x: 1, y: -1)
  context.translateBy(x: offset.x, y: offset.y)
  context.scaleBy(x: scale, y: scale)
  for part in record.path {
    context.addPath(try pathFromSVG(part.d))
  }
  context.fillPath()

  var packed = [UInt8](repeating: 0, count: maskBytes)
  for y in 0..<canvasHeight {
    for x in 0..<canvasWidth where pixels[y * canvasWidth + x] < 160 {
      packed[y * (canvasWidth / 8) + x / 8] |= UInt8(1 << (7 - (x & 7)))
    }
  }
  guard packed.contains(where: { $0 != 0 }) else { fatalError("empty icon mask") }
  return packed
}

func hex(_ byte: UInt8) -> String { String(format: "0x%02X", byte) }

var output = "// Generated from iconfont.cn SVG paths.\n#pragma once\n#include <Arduino.h>\n\n"
output += "constexpr uint16_t kWeatherIconWidth = \(canvasWidth);\n"
output += "constexpr uint16_t kWeatherIconHeight = \(canvasHeight);\n"
output += "constexpr uint16_t kWeatherIconBytesPerRow = \(canvasWidth / 8);\n\n"

for spec in specs {
  guard let record = records[spec.sourceName] else { fatalError("missing icon \(spec.sourceName)") }
  let bytes = try rasterize(record)
  output += "const uint8_t kWeatherIcon\(spec.symbol)[\(bytes.count)] PROGMEM = {\n"
  for start in stride(from: 0, to: bytes.count, by: 16) {
    let end = min(start + 16, bytes.count)
    output += "  " + bytes[start..<end].map(hex).joined(separator: ", ") + (end == bytes.count ? "\n" : ",\n")
  }
  output += "};\n\n"
}

try output.write(to: outputURL, atomically: true, encoding: .utf8)
print("generated \(specs.count) weather masks at \(outputURL.path)")
