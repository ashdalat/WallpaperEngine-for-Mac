import AppKit
import CoreGraphics
import QuartzCore

/// Renders Wallpaper Engine "scene" (3D) wallpapers.
/// Hosts a CAMetalLayer that the wallpaper-scene-renderer
/// engine presents into via MoltenVK.
final class DesktopSceneView: NSView {
    private var bridge: SceneRendererBridge?
    private var mouseEnabled = false

    override init(frame: NSRect) {
        super.init(frame: frame)
        setup()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setup()
    }

    private func setup() {
        // Layer-hosting view: the engine renders straight into the
        // CAMetalLayer, AppKit must not draw on top of it.
        wantsLayer = true
        layer = CAMetalLayer()
    }

    func load(item: WallpaperItem, assetsURL: URL, cacheURL: URL, settings: AppSettings) {
        guard let metalLayer = layer as? CAMetalLayer,
              item.contentURL != nil else { return }

        // The engine creates a fixed-size Vulkan swapchain for the
        // layer, so match the layer's pixel size exactly.
        let scale = window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 1.0
        metalLayer.contentsScale = scale
        let width = UInt32(max(1, (bounds.width * scale).rounded(.up)))
        let height = UInt32(max(1, (bounds.height * scale).rounded(.up)))

        fputs("WEM: scene view load \(width)x\(height)\n", stderr)
        let bridge = SceneRendererBridge(metalLayer: metalLayer, width: width, height: height)
        self.bridge = bridge

        let displayMode = DisplayMode(rawValue: settings.displayMode) ?? .crop
        bridge.setFillMode(Int32(displayMode.fillModeRawValue))
        bridge.setFps(30)
        bridge.setMuted(settings.muteAudio)
        bridge.setVolume(Float(settings.volume))
        bridge.loadWallpaper(at: item.contentURL!, assetsURL: assetsURL, cacheURL: cacheURL)
    }

    func setMouseEnabled(_ enabled: Bool) {
        mouseEnabled = enabled
    }

    func play() {
        fputs("WEM: scene play\n", stderr)
        bridge?.play()
    }

    func pause() {
        fputs("WEM: scene pause\n", stderr)
        bridge?.pause()
    }

    func updateSettings(_ settings: AppSettings) {
        let displayMode = DisplayMode(rawValue: settings.displayMode) ?? .crop
        bridge?.setFillMode(Int32(displayMode.fillModeRawValue))
        bridge?.setMuted(settings.muteAudio)
        bridge?.setVolume(Float(settings.volume))
    }

    // MARK: - Mouse forwarding

    func handleMouseEvent(_ event: CGEvent, window: NSWindow) {
        guard mouseEnabled, let bridge = bridge, bounds.width > 0, bounds.height > 0 else { return }

        // NSEvent.mouseLocation is in Cocoa screen coordinates
        // (origin at bottom-left).
        let mouse = NSEvent.mouseLocation
        let windowPoint = window.convertPoint(fromScreen: mouse)
        let viewPoint = convert(windowPoint, from: nil)

        let nx = (viewPoint.x / bounds.width).clamped(to: 0...1)
        // The engine measures y from the TOP of the view.
        let ny = (1.0 - viewPoint.y / bounds.height).clamped(to: 0...1)
        bridge.mouseInputX(Double(nx), y: Double(ny))
    }
}

private extension Double {
    func clamped(to range: ClosedRange<Double>) -> Double {
        min(max(self, range.lowerBound), range.upperBound)
    }
}
