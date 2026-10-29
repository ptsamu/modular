import AppKit

guard CommandLine.arguments.count == 3,
      let logo = NSImage(contentsOfFile: CommandLine.arguments[1]) else
{
    fatalError("Usage: generate_modular_icon.swift <logo.png> <output.png>")
}

let canvas = NSSize(width: 1024, height: 1024)
let output = NSImage(size: canvas)
output.lockFocus()

let iconRect = NSRect(x: 58, y: 58, width: 908, height: 908)
let iconPath = NSBezierPath(roundedRect: iconRect, xRadius: 218, yRadius: 218)
let shadow = NSShadow()
shadow.shadowColor = NSColor.black.withAlphaComponent(0.26)
shadow.shadowOffset = NSSize(width: 0, height: -18)
shadow.shadowBlurRadius = 34
shadow.set()

let orange = NSColor(calibratedRed: 0.922, green: 0.424, blue: 0.165, alpha: 1.0)
let orangeLight = NSColor(calibratedRed: 1.0, green: 0.53, blue: 0.25, alpha: 1.0)
NSGradient(starting: orangeLight, ending: orange)?.draw(in: iconPath, angle: -90)

NSGraphicsContext.current?.cgContext.saveGState()
iconPath.addClip()
let highlight = NSGradient(starting: NSColor.white.withAlphaComponent(0.20), ending: NSColor.clear)
highlight?.draw(in: NSRect(x: 58, y: 512, width: 908, height: 454), angle: -90)
NSGraphicsContext.current?.cgContext.restoreGState()

let logoRect = NSRect(x: 248, y: 248, width: 528, height: 528)
logo.draw(in: logoRect, from: NSRect(origin: .zero, size: logo.size), operation: .sourceOver, fraction: 1.0)
NSGraphicsContext.current?.cgContext.saveGState()
NSGraphicsContext.current?.cgContext.setBlendMode(.sourceIn)
NSColor(calibratedWhite: 0.94, alpha: 1.0).setFill()
NSBezierPath(rect: logoRect).fill()
NSGraphicsContext.current?.cgContext.restoreGState()

output.unlockFocus()
let bitmap = NSBitmapImageRep(data: output.tiffRepresentation!)!
try bitmap.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: CommandLine.arguments[2]))
