import AVFoundation
import AppKit
import ScreenCaptureKit

/// Captures a full-resolution still from a video or web
/// wallpaper's first paint and installs it as the real
/// macOS desktop wallpaper (NSWorkspace). While the app
/// runs, the animated window covers the desktop; when
/// the app quits or the wallpaper is removed, the still
/// keeps the desktop from going black.
///
/// Scene wallpapers are not covered here — their
/// CAMetalLayer content cannot be snapshotted through
/// the window-capture APIs; that needs a blit inside
/// the render loop.
@MainActor
enum StaticWallpaper {
    /// One capture per wallpaper id per session.
    /// Re-applying the same wallpaper (screen
    /// changes, settings writes) reuses the result
    /// instead of re-decoding the video.
    private static var capturedIDs = Set<String>()

    private static var cacheDirectory: URL {
        let dir = FileManager.default
            .urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("WallpaperEngineMac/snapshots")
        try? FileManager.default.createDirectory(
            at: dir, withIntermediateDirectories: true)
        return dir
    }

    /// Runs `capture` at most once per wallpaper id
    /// per session; later calls for the same id
    /// return without running it.
    static func captureOnce(for item: WallpaperItem, _ capture: () -> Void) {
        guard !capturedIDs.contains(item.id) else { return }
        capturedIDs.insert(item.id)
        capture()
    }

    /// Decodes the video's first frame at its native
    /// resolution (not the preview.gif's 192–256 px).
    static func captureVideoFrame(url: URL,
                                  completion: @escaping (CGImage?) -> Void) {
        let generator = AVAssetImageGenerator(asset: AVURLAsset(url: url))
        generator.appliesPreferredTrackTransform = true
        // Cap the decoded frame: 4K videos decode
        // fine, but an 8K source would produce a
        // PNG too large to be worth writing.
        generator.maximumSize = CGSize(width: 3840, height: 3840)
        generator.generateCGImagesAsynchronously(forTimes: [NSValue(time: .zero)]) {
            _, image, _, result, error in
            if let error {
                fputs("WEM: static snapshot decode failed: \(error.localizedDescription)\n", stderr)
            }
            completion(result == .succeeded ? image : nil)
        }
    }

    /// Captures the composited content of one of the
    /// app's own windows. Used for scene wallpapers:
    /// their CAMetalLayer content comes back black
    /// through the legacy window-capture APIs, but
    /// ScreenCaptureKit sees the composited output.
    /// Check CGPreflightScreenCaptureAccess() first to avoid
    /// triggering unwanted system permission alerts on macOS 14.4+/15.
    static func captureWindow(_ window: NSWindow,
                              completion: @escaping (CGImage?) -> Void) {
        guard #available(macOS 14.0, *) else {
            completion(nil)
            return
        }
        guard CGPreflightScreenCaptureAccess() else {
            completion(nil)
            return
        }
        let windowID = CGWindowID(window.windowNumber)
        Task {
            do {
                // Without screen-recording permission this
                // enumeration only ever sees this app's
                // windows — exactly what is needed here.
                let content = try await SCShareableContent
                    .excludingDesktopWindows(false, onScreenWindowsOnly: true)
                guard let scWindow = content.windows
                    .first(where: { $0.windowID == windowID }) else {
                    completion(nil)
                    return
                }
                let filter = SCContentFilter(desktopIndependentWindow: scWindow)
                let config = SCStreamConfiguration()
                config.width = Int(window.frame.width * window.backingScaleFactor)
                config.height = Int(window.frame.height * window.backingScaleFactor)
                config.scalesToFit = false
                config.ignoreShadowsSingleWindow = true
                let image = try await SCScreenshotManager
                    .captureImage(contentFilter: filter, configuration: config)
                completion(image)
            } catch {
                fputs("WEM: scene window capture failed: \(error.localizedDescription)\n", stderr)
                completion(nil)
            }
        }
    }

    /// True when every sampled pixel is identical —
    /// a Metal layer still blank (uniform clear color)
    /// before the scene's first real frame.
    static func isUniform(_ image: CGImage) -> Bool {
        let width = 16, height = 16
        guard let context = CGContext(
            data: nil, width: width, height: height,
            bitsPerComponent: 8, bytesPerRow: width * 4,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
        else { return false }
        context.interpolationQuality = .low
        context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
        guard let data = context.data else { return false }
        let pixels = data.bindMemory(to: UInt8.self, capacity: width * height * 4)
        let first = (pixels[0], pixels[1], pixels[2])
        for i in stride(from: 4, to: width * height * 4, by: 4) {
            if (pixels[i], pixels[i + 1], pixels[i + 2]) != first {
                return false
            }
        }
        return true
    }

    static func install(_ image: CGImage,
                        for item: WallpaperItem,
                        screen: NSScreen,
                        displayMode: DisplayMode) {
        let rep = NSBitmapImageRep(cgImage: image)
        guard let data = rep.representation(using: .png, properties: [:]) else { return }
        let url = cacheDirectory.appendingPathComponent("\(item.id).png")
        // Atomic: on a multi-screen setup both
        // screens' renderers write the same file.
        do {
            try data.write(to: url, options: .atomic)
        } catch {
            fputs("WEM: static snapshot write failed: \(error.localizedDescription)\n", stderr)
            return
        }
        // The C constants don't import into Swift
        // as DesktopImageOptionKey members, and
        // their runtime values equal their symbol
        // names (verified against the AppKit
        // constants), so they are spelled out here.
        var options: [NSWorkspace.DesktopImageOptionKey: Any] = [
            .init(rawValue: "NSWorkspaceDesktopImageScalingKey"):
                NSImageScaling.scaleProportionallyUpOrDown.rawValue
        ]
        if displayMode == .crop {
            // macOS has no direct "fill" scaling;
            // proportionally-up-or-down plus
            // clipping is what System Settings'
            // "Fill Screen" uses.
            options[.init(rawValue: "NSWorkspaceDesktopImageAllowClippingKey")] = true
        }
        do {
            try NSWorkspace.shared.setDesktopImageURL(
                url, for: screen, options: options)
            fputs("WEM: static wallpaper installed \(item.id) (\(image.width)x\(image.height))\n", stderr)
        } catch {
            fputs("WEM: setDesktopImageURL failed: \(error.localizedDescription)\n", stderr)
        }
    }
}
