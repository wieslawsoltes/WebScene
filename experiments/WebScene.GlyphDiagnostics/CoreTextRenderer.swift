import AppKit
import CoreText
import Foundation
import ImageIO
import UniformTypeIdentifiers

struct Configuration: Decodable {
    let width: Int
    let height: Int
    let background: String
    let foreground: String
    let family: String
    let cases: [GlyphCase]
}

struct GlyphCase: Decodable {
    let id: String
    let text: String
    let size: CGFloat
    let weight: Int
    let x: CGFloat
    let baseline: CGFloat
}

struct RunMetrics: Encodable {
    let id: String
    let glyphs: [UInt16]
    let positions: [[CGFloat]]
    let advances: [[CGFloat]]
    let width: Double
    let ascent: CGFloat
    let descent: CGFloat
    let leading: CGFloat
}

func color(_ value: String, in colorSpace: CGColorSpace) -> CGColor {
    let hex = value.dropFirst()
    let number = UInt32(hex, radix: 16)!
    return CGColor(
        colorSpace: colorSpace,
        components: [
            CGFloat((number >> 16) & 0xff) / 255,
            CGFloat((number >> 8) & 0xff) / 255,
            CGFloat(number & 0xff) / 255,
            1
        ])!
}

func systemWeight(_ value: Int) -> NSFont.Weight {
    switch value {
    case ...149: return .ultraLight
    case 150...249: return .thin
    case 250...349: return .light
    case 350...449: return .regular
    case 450...549: return .medium
    case 550...649: return .semibold
    case 650...749: return .bold
    case 750...849: return .heavy
    default: return .black
    }
}

guard CommandLine.arguments.count == 5 else {
    fputs("usage: CoreTextRenderer <cases.json> <scale> <output.png> <metrics.json>\n", stderr)
    exit(2)
}

let casesURL = URL(fileURLWithPath: CommandLine.arguments[1])
let scale = CGFloat(Double(CommandLine.arguments[2])!)
let outputURL = URL(fileURLWithPath: CommandLine.arguments[3])
let metricsURL = URL(fileURLWithPath: CommandLine.arguments[4])
let config = try JSONDecoder().decode(Configuration.self, from: Data(contentsOf: casesURL))
let pixelWidth = Int(CGFloat(config.width) * scale)
let pixelHeight = Int(CGFloat(config.height) * scale)
let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!
guard let context = CGContext(
    data: nil,
    width: pixelWidth,
    height: pixelHeight,
    bitsPerComponent: 8,
    bytesPerRow: pixelWidth * 4,
    space: colorSpace,
    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
else { fatalError("Could not create the CoreGraphics bitmap context") }

context.setFillColor(color(config.background, in: colorSpace))
context.fill(CGRect(x: 0, y: 0, width: pixelWidth, height: pixelHeight))
context.setAllowsAntialiasing(true)
context.setShouldAntialias(true)
context.setAllowsFontSmoothing(true)
context.setShouldSmoothFonts(true)
context.setAllowsFontSubpixelPositioning(scale >= 1.5)
context.setShouldSubpixelPositionFonts(scale >= 1.5)
context.setAllowsFontSubpixelQuantization(scale < 1.5)
context.setShouldSubpixelQuantizeFonts(scale < 1.5)
context.translateBy(x: 0, y: CGFloat(pixelHeight))
context.scaleBy(x: scale, y: -scale)
context.textMatrix = CGAffineTransform(scaleX: 1, y: -1)

var allMetrics: [RunMetrics] = []
let foregroundColor = color(config.foreground, in: colorSpace)
for item in config.cases {
    let font = NSFont.systemFont(ofSize: item.size, weight: systemWeight(item.weight))
    let attributes: [NSAttributedString.Key: Any] = [
        NSAttributedString.Key(kCTFontAttributeName as String): font,
        NSAttributedString.Key(kCTForegroundColorAttributeName as String): foregroundColor,
        NSAttributedString.Key(kCTKernAttributeName as String): 0
    ]
    let line = CTLineCreateWithAttributedString(
        NSAttributedString(string: item.text, attributes: attributes))
    context.textPosition = CGPoint(x: item.x, y: item.baseline)
    CTLineDraw(line, context)

    var ascent: CGFloat = 0
    var descent: CGFloat = 0
    var leading: CGFloat = 0
    let width = CTLineGetTypographicBounds(line, &ascent, &descent, &leading)
    var glyphs: [UInt16] = []
    var positions: [[CGFloat]] = []
    var advances: [[CGFloat]] = []
    let runs = CTLineGetGlyphRuns(line) as! [CTRun]
    for run in runs {
        let count = CTRunGetGlyphCount(run)
        var runGlyphs = Array(repeating: CGGlyph(), count: count)
        var runPositions = Array(repeating: CGPoint.zero, count: count)
        var runAdvances = Array(repeating: CGSize.zero, count: count)
        CTRunGetGlyphs(run, CFRange(location: 0, length: 0), &runGlyphs)
        CTRunGetPositions(run, CFRange(location: 0, length: 0), &runPositions)
        CTRunGetAdvances(run, CFRange(location: 0, length: 0), &runAdvances)
        glyphs.append(contentsOf: runGlyphs.map { UInt16($0) })
        positions.append(contentsOf: runPositions.map { [$0.x, $0.y] })
        advances.append(contentsOf: runAdvances.map { [$0.width, $0.height] })
    }
    allMetrics.append(RunMetrics(
        id: item.id,
        glyphs: glyphs,
        positions: positions,
        advances: advances,
        width: width,
        ascent: ascent,
        descent: descent,
        leading: leading))
}

guard let image = context.makeImage(),
      let destination = CGImageDestinationCreateWithURL(
        outputURL as CFURL,
        UTType.png.identifier as CFString,
        1,
        nil)
else { fatalError("Could not create the PNG destination") }
CGImageDestinationAddImage(destination, image, nil)
guard CGImageDestinationFinalize(destination) else { fatalError("Could not write the PNG") }
try JSONEncoder().encode(allMetrics).write(to: metricsURL)
