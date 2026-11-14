// The Asam app icon for macOS: the Luncur mark in white on an ink tile (Rapi ink, #151515).
//
// The tile follows Apple's app icon grid for macOS 11 to 15, measured from the system icons:
// a 1024 canvas, an 824 tile inset 100 with continuous corners of radius 185.4, and the shared
// drop shadow, black at 30 %, 10 down, Gaussian sigma 10. The mark keeps its Rapi clear space,
// half its height, between it and the tile's left and right edges, and sits in the centre.
//
// Run from the repository root:  swift Resources/Icon/asam-app-icon.swift
// It writes asam-app-icon.png beside this file; CMake hands that to JUCE as ICON_BIG.
// JUCE builds the .icns when CMake configures and signs the bundles only when they link,
// so after changing the PNG rebuild the bundles from scratch:
//   rm -rf build/Asam_artefacts && cmake -B build && cmake --build build

import CoreGraphics
import Foundation
import ImageIO

let canvas = 1024
let tile = CGRect (x: 100, y: 100, width: 824, height: 824)
let cornerRadius: CGFloat = 185.4
let ink = CGColor (srgbRed: 0x15 / 255.0, green: 0x15 / 255.0, blue: 0x15 / 255.0, alpha: 1)
let white = CGColor (srgbRed: 1, green: 1, blue: 1, alpha: 1)
let shadowColour = CGColor (srgbRed: 0, green: 0, blue: 0, alpha: 0.3)
let shadowDrop: CGFloat = 10
let shadowBlur: CGFloat = 22  // Core Graphics blur 22 matches that sigma-10 profile to within 1/255

/// Luncur: a low note slides up to its octave. 147.3 x 100, each note one stem (46.5) wide.
let luncur: [CGPoint] = [(0, 50), (46.5, 50), (100.8, 0), (147.3, 0), (147.3, 50), (100.8, 50), (46.5, 100), (0, 100)]
    .map { CGPoint (x: $0.0, y: $0.1) }

/// Apple's continuous-corner rounded rectangle (the iOS 7 squircle), in y-down coordinates.
func continuousRoundedRect (_ rect: CGRect, _ r: CGFloat) -> CGPath
{
    let reach: CGFloat = 1.52866483
    // Each corner runs from the incoming edge to the outgoing one; (s, t) are distances from
    // the corner along those edges, in units of r. Lines are 2 numbers, curves 6.
    let corner: [[CGFloat]] = [[1.08849323, 0, 0.86840689, 0, 0.66993427, 0.06549600],
                               [0.63149399, 0.07491100],
                               [0.37282392, 0.16905899, 0.16906013, 0.37282401, 0.07491100, 0.63149399],
                               [0.06549600, 0.66993427],
                               [0, 0.86840689, 0, 1.08849323, 0, reach]]
    let corners: [(CGPoint, CGVector, CGVector)] = [
        (CGPoint (x: rect.maxX, y: rect.minY), CGVector (dx: -1, dy: 0), CGVector (dx: 0, dy: 1)),
        (CGPoint (x: rect.maxX, y: rect.maxY), CGVector (dx: 0, dy: -1), CGVector (dx: -1, dy: 0)),
        (CGPoint (x: rect.minX, y: rect.maxY), CGVector (dx: 1, dy: 0), CGVector (dx: 0, dy: -1)),
        (CGPoint (x: rect.minX, y: rect.minY), CGVector (dx: 0, dy: 1), CGVector (dx: 1, dy: 0))]

    let path = CGMutablePath()

    for (index, (c, back, out)) in corners.enumerated()
    {
        func at (_ s: CGFloat, _ t: CGFloat) -> CGPoint
        {
            CGPoint (x: c.x + (s * back.dx + t * out.dx) * r, y: c.y + (s * back.dy + t * out.dy) * r)
        }

        if index == 0 { path.move (to: at (reach, 0)) } else { path.addLine (to: at (reach, 0)) }

        for step in corner
        {
            if step.count == 2 { path.addLine (to: at (step[0], step[1])) }
            else { path.addCurve (to: at (step[4], step[5]), control1: at (step[0], step[1]), control2: at (step[2], step[3])) }
        }
    }

    path.closeSubpath()
    return path
}

let context = CGContext (data: nil, width: canvas, height: canvas, bitsPerComponent: 8, bytesPerRow: 0,
                         space: CGColorSpace (name: CGColorSpace.sRGB)!,
                         bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!

context.translateBy (x: 0, y: CGFloat (canvas))   // y down, like SVG
context.scaleBy (x: 1, y: -1)

// The tile and its shadow. Shadow offsets are in device space, where y points up.
context.saveGState()
context.setShadow (offset: CGSize (width: 0, height: -shadowDrop), blur: shadowBlur, color: shadowColour)
context.addPath (continuousRoundedRect (tile, cornerRadius))
context.setFillColor (ink)
context.fillPath()
context.restoreGState()

// The mark: clear space (50 units) left and right, so the tile is 247.3 units wide.
let scale = tile.width / (147.3 + 100)
let origin = CGPoint (x: tile.minX + 50 * scale, y: tile.midY - 50 * scale)
let mark = CGMutablePath()
mark.addLines (between: luncur.map { CGPoint (x: origin.x + $0.x * scale, y: origin.y + $0.y * scale) })
mark.closeSubpath()
context.addPath (mark)
context.setFillColor (white)
context.fillPath()

let output = URL (fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent ("asam-app-icon.png")
let destination = CGImageDestinationCreateWithURL (output as CFURL, "public.png" as CFString, 1, nil)!
CGImageDestinationAddImage (destination, context.makeImage()!, nil)
precondition (CGImageDestinationFinalize (destination), "could not write \(output.path)")
print ("wrote \(output.path)")
