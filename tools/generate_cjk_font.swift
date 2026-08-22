import CoreGraphics
import CoreText
import Foundation

let output = CommandLine.arguments[1]
let characters = Array("深圳天气今明后大温度湿体感晴多云阴小雨中雷雪雾最高低降水概率日出落更新失败当前星期一二三四五六七近小时周").map(String.init)
let fontName = "Hiragino Sans GB W3" as CFString
let font = CTFontCreateWithName(fontName, 15, nil)
let colorSpace = CGColorSpaceCreateDeviceGray()

struct GlyphData {
  let codepoint: UInt32
  let bytes: [UInt8]
}

var glyphs: [GlyphData] = []
for character in characters {
  let scalars = Array(character.unicodeScalars)
  guard let scalar = scalars.first else { continue }
  var pixels = [UInt8](repeating: 255, count: 16 * 16)
  guard let context = CGContext(data: &pixels, width: 16, height: 16,
                                bitsPerComponent: 8, bytesPerRow: 16,
                                space: colorSpace, bitmapInfo: 0) else {
    fatalError("Unable to create glyph context")
  }
  context.setFillColor(gray: 0, alpha: 1)
  let text = character as CFString
  let fontAttribute = NSAttributedString.Key(kCTFontAttributeName as String)
  let attributed = NSAttributedString(string: text as String,
                                        attributes: [fontAttribute: font])
  let line = CTLineCreateWithAttributedString(attributed)
  context.saveGState()
  context.translateBy(x: 0, y: 16)
  context.scaleBy(x: 1, y: -1)
  CTLineDraw(line, context)
  context.restoreGState()

  var packed = [UInt8](repeating: 0, count: 32)
  for y in 0..<16 {
    let sourceY = 15 - y
    for x in 0..<16 {
      if pixels[sourceY * 16 + x] < 160 {
        // Horizontal bit order: bit 7 is display column 0.
        packed[y * 2 + x / 8] |= UInt8(1 << (7 - (x & 7)))
      }
    }
  }
  glyphs.append(GlyphData(codepoint: scalar.value, bytes: packed))
}

var text = "// Generated 16x16 CJK glyphs for the Shenzhen weather dashboard.\n"
text += "// Horizontal bit order: bit 7 is display column 0.\n"
text += "// Vertical row order: row 0 is the top display row.\n"
text += "#pragma once\n#include <Arduino.h>\n\n"
text += "struct WeatherGlyph { uint32_t codepoint; uint8_t bitmap[32]; };\n"
text += "const WeatherGlyph kWeatherFont[] PROGMEM = {\n"
for glyph in glyphs {
  text += "  { 0x\(String(glyph.codepoint, radix: 16, uppercase: true)), { "
  text += glyph.bytes.map { String(format: "0x%02X", $0) }.joined(separator: ", ")
  text += " } },\n"
}
text += "};\n"
text += "const uint8_t kWeatherFontCount = \(glyphs.count);\n"
try! text.write(toFile: output, atomically: true, encoding: .utf8)
print("wrote \(output): \(glyphs.count) glyphs")
