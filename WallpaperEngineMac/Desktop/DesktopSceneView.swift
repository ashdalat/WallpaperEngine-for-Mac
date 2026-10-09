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
        // The engine starts with the cursor "outside"
        // — re-sync so the next mouse event reports
        // the real state.
        cursorInWindow = false

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

    /// Applies one Wallpaper Engine user property to the
    /// running scene (shader uniforms, textures, clear
    /// color). See the project.json "properties" table.
    func setUserProperty(_ key: String, value: String) {
        bridge?.setUserProperty(key, value: value)
    }

    // MARK: - Mouse forwarding

    private var mouseEventCount = 0
    // Last value reported to the engine via
    // mouseEnter — only changes are sent.
    private var cursorInWindow = false

    func handleMouseEvent(_ event: CGEvent, window: NSWindow) {
        guard mouseEnabled, let bridge = bridge, bounds.width > 0, bounds.height > 0 else {
            if mouseEnabled {
                fputs("WEM: scene mouse event dropped (no bridge/zero size)\n", stderr)
            }
            return
        }

        mouseEventCount += 1
        if mouseEventCount < 5 || mouseEventCount % 100 == 0 {
            fputs("WEM: scene mouse event #\(mouseEventCount)\n", stderr)
        }

        // Interactive scenes need the click itself,
        // not just the position.
        switch event.type {
        case .leftMouseDown:
            bridge.mouseButton(0, down: true)
        case .leftMouseUp:
            bridge.mouseButton(0, down: false)
        case .rightMouseDown:
            bridge.mouseButton(1, down: true)
        case .rightMouseUp:
            bridge.mouseButton(1, down: false)
        case .otherMouseDown:
            bridge.mouseButton(buttonNumber(of: event), down: true)
        case .otherMouseUp:
            bridge.mouseButton(buttonNumber(of: event), down: false)
        default:
            break
        }

        // NSEvent.mouseLocation is in Cocoa screen coordinates
        // (origin at bottom-left).
        let mouse = NSEvent.mouseLocation
        let windowPoint = window.convertPoint(fromScreen: mouse)
        let viewPoint = convert(windowPoint, from: nil)

        let nx = (viewPoint.x / bounds.width).clamped(to: 0...1)
        // The engine measures y from the TOP of the view.
        let ny = (1.0 - viewPoint.y / bounds.height).clamped(to: 0...1)
        bridge.mouseInputX(Double(nx), y: Double(ny))

        // The window ignoresMouseEvents (it sits below the
        // desktop icons), so tracking areas never fire —
        // derive "cursor in window" from the cursor's
        // screen instead. Only report changes.
        let inWindow = window.screen.map {
            NSMouseInRect(mouse, $0.frame, false)
        } ?? false
        if inWindow != cursorInWindow {
            cursorInWindow = inWindow
            bridge.mouseEnter(inWindow)
        }
    }

    private func buttonNumber(of event: CGEvent) -> Int32 {
        Int32(clamping: event.getIntegerValueField(.mouseEventButtonNumber))
    }
}

private extension Double {
    func clamped(to range: ClosedRange<Double>) -> Double {
        min(max(self, range.lowerBound), range.upperBound)
    }
}
