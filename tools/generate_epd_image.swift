import CoreGraphics
import Foundation
import ImageIO

let input = CommandLine.arguments[1]
let output = CommandLine.arguments[2]
let symbol = CommandLine.arguments.count > 3 ? CommandLine.arguments[3] : "kSunsetImage"
let width = 768
let height = 552
let rowBytes = width / 4

guard let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: input) as CFURL, nil),
      let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
  fatalError("Unable to decode input image")
}

let colorSpace = CGColorSpaceCreateDeviceRGB()
var pixels = [UInt8](repeating: 255, count: width * height * 4)
guard let context = CGContext(data: &pixels, width: width, height: height,
                              bitsPerComponent: 8, bytesPerRow: width * 4,
                              space: colorSpace,
                              bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
  fatalError("Unable to create output bitmap")
}

// Center-crop the source to the panel's 768:552 aspect ratio, then scale.
let sourceWidth = CGFloat(image.width)
let sourceHeight = CGFloat(image.height)
let targetAspect = CGFloat(width) / CGFloat(height)
let sourceAspect = sourceWidth / sourceHeight
let crop: CGRect
if sourceAspect > targetAspect {
  let cropWidth = sourceHeight * targetAspect
  crop = CGRect(x: (sourceWidth - cropWidth) / 2, y: 0,
                width: cropWidth, height: sourceHeight)
} else {
  let cropHeight = sourceWidth / targetAspect
  crop = CGRect(x: 0, y: (sourceHeight - cropHeight) / 2,
                width: sourceWidth, height: cropHeight)
}
context.interpolationQuality = .high
// Redraw through a cropped image so the source composition fills the panel.
context.clear(CGRect(x: 0, y: 0, width: width, height: height))
guard let cropped = image.cropping(to: crop) else {
  fatalError("Unable to crop input image")
}
// Make bitmap row 0 correspond to the top row of the photograph.
context.translateBy(x: 0, y: CGFloat(height))
context.scaleBy(x: 1, y: -1)
context.draw(cropped, in: CGRect(x: 0, y: 0, width: width, height: height))

// Palette order matches the firmware: BLACK=0, WHITE=1, YELLOW=2, RED=3.
let palette = [(20, 20, 20), (242, 242, 232), (220, 178, 54), (174, 52, 45)]
let bayer: [[Int]] = [[0, 2], [3, 1]]
var packed = [UInt8](repeating: 0, count: width * height / 4)
for y in 0..<height {
  for x in 0..<width {
    let offset = (y * width + x) * 4
    let dither = bayer[y & 1][x & 1] - 1
    let r = Int(pixels[offset]) + dither * 7
    let g = Int(pixels[offset + 1]) + dither * 7
    let b = Int(pixels[offset + 2]) + dither * 7
    var best = 0
    var bestDistance = Int.max
    for (index, color) in palette.enumerated() {
      let dr = r - color.0
      let dg = g - color.1
      let db = b - color.2
      let distance = dr * dr * 2 + dg * dg * 3 + db * db
      if distance < bestDistance {
        bestDistance = distance
        best = index
      }
    }
    let byteIndex = y * rowBytes + x / 4
    packed[byteIndex] |= UInt8(best) << (6 - (x & 3) * 2)
  }
}

var text = "// Generated from Wikimedia Commons sunset photograph.\n"
text += "// 768x552, 2 bits/pixel, BLACK=0 WHITE=1 YELLOW=2 RED=3.\n"
text += "#pragma once\n#include <Arduino.h>\n\n"
text += "const uint8_t \(symbol)[\(packed.count)] PROGMEM = {\n"
for index in stride(from: 0, to: packed.count, by: 16) {
  let end = min(index + 16, packed.count)
  text += "  " + packed[index..<end].map { String(format: "0x%02X", $0) }.joined(separator: ", ")
  text += end == packed.count ? "\n" : ",\n"
}
text += "};\n"
try! text.write(toFile: output, atomically: true, encoding: .utf8)
print("wrote \(output): \(packed.count) bytes")
