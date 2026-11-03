import AppKit

@MainActor
final class DesktopWindowManager: NSObject {
    static let shared = DesktopWindowManager()

    private var windows: [String: DesktopWallpaperWindow] = [:]
    private var renderers: [String: WallpaperRenderer] = [:]

    override private init() {
        super.init()
    }

    func applyToAllScreens(_ item: WallpaperItem?, settings: AppSettings) {
        for screen in NSScreen.screens {
            apply(item, to: screen, settings: settings)
        }
    }

    func apply(_ item: WallpaperItem?, to screen: NSScreen, settings: AppSettings) {
        let key = screenKey(screen)

        if item == nil {
            renderers[key]?.unload()
            renderers[key] = nil
            windows[key]?.close()
            windows[key] = nil
            return
        }

        let window: DesktopWallpaperWindow
        if let existing = windows[key] {
            window = existing
            window.updateFrame()
        } else {
            window = DesktopWallpaperWindow(screen: screen)
            window.orderFront(nil)
            windows[key] = window
        }

        // Video→video switch on this screen: hand the
        // running video view to the next renderer so
        // its player keeps showing the old frame
        // until the new wallpaper renders (no black
        // gap). Everything happens inside one
        // CATransaction — detach from the old host
        // and re-attach to the new one commit
        // together, no intermediate frame.
        let previous = renderers[key]
        let carriedVideoView: DesktopVideoView?
        let carriedWebView: DesktopWebView?
        let carriedSceneView: DesktopSceneView?
        if item!.type == .video, previous?.currentItem?.type == .video {
            carriedVideoView = previous?.takeVideoView()
            carriedWebView = nil
            carriedSceneView = nil
        } else if item!.type == .web, previous?.currentItem?.type == .web {
            carriedVideoView = nil
            carriedWebView = previous?.takeWebView()
            carriedSceneView = nil
        } else if item!.type == .scene, previous?.currentItem?.type == .scene {
            carriedVideoView = nil
            carriedWebView = nil
            carriedSceneView = previous?.takeSceneView()
        } else {
            carriedVideoView = nil
            carriedWebView = nil
            carriedSceneView = nil
        }

        let renderer = WallpaperRenderer()
        previous?.unload()
        renderers[key] = renderer
        fputs("WEM: apply renderer for \(item!.title) on screen\n", stderr)
        renderer.load(item: item!, in: window, settings: settings,
                      carryingVideoView: carriedVideoView,
                      carryingWebView: carriedWebView,
                      carryingSceneView: carriedSceneView)

        if let seconds = Double(
            ProcessInfo.processInfo.environment["WEM_DEBUG_BLACKDETECT"] ?? ""),
           seconds > 0 {
            Self.blackDetect(window: window, remaining: seconds)
        }
        // The pause policy only fires onChange on a
        // state transition, so at startup (already
        // unpaused) resumeAll() never runs and the
        // scene — and its audio — never got play().
        // Rendering starts from load(), but the
        // engine's SoundManager only plays on play().
        renderer.play()
    }

    func pauseAll() {
        renderers.values.forEach { $0.pause() }
    }

    /// Debug hook (WEM_DEBUG_BLACKDETECT=seconds):
    /// samples the wallpaper window every ~300 ms and
    /// logs whether the frame is uniform — used to
    /// verify switches do not flash a black frame.
    private static func blackDetect(window: NSWindow, remaining: Double) {
        guard remaining > 0 else { return }
        StaticWallpaper.captureWindow(window) { image in
            DispatchQueue.main.async {
                let uniform = image.map(StaticWallpaper.isUniform) ?? true
                fputs("WEM: blackdetect uniform=\(uniform)\n", stderr)
                blackDetect(window: window, remaining: remaining - 0.3)
            }
        }
    }

    func resumeAll() {
        renderers.values.forEach { $0.play() }
    }

    /// Pushes settings to the live renderers
    /// without reloading the wallpaper. Used for
    /// display transform changes (rotation, flip,
    /// position) — those are pure layer
    /// operations, so a reload's black gap and
    /// scene restart would be pointless cost.
    func updateSettings(_ settings: AppSettings) {
        for renderer in renderers.values {
            renderer.updateSettings(settings)
        }
    }

    func handleScreenConfigurationChange(settings: AppSettings, current: WallpaperItem?) {
        let activeScreens = Set(NSScreen.screens.map(screenKey))
        let staleKeys = windows.keys.filter { !activeScreens.contains($0) }
        for key in staleKeys {
            renderers[key]?.unload()
            renderers[key] = nil
            windows[key]?.close()
            windows[key] = nil
        }
        if let current = current {
            for screen in NSScreen.screens {
                let key = screenKey(screen)
                if windows[key] != nil {
                    // This screen already shows the wallpaper.
                    // Only track the new frame: rebuilding the
                    // renderer here tears down the whole Vulkan
                    // device and leaves a black gap on screen
                    // until the scene is reloaded.
                    windows[key]?.updateFrame()
                } else {
                    apply(current, to: screen, settings: settings)
                }
            }
        }
    }

    func handleMouseEvent(_ event: CGEvent) {
        for (key, renderer) in renderers {
            guard let window = windows[key] else { continue }
            renderer.handleMouseEvent(event, window: window)
        }
    }

    /// Forwards a user property change to every screen
    /// showing `item` (a wallpaper set to all screens).
    func setUserProperty(key: String, value: String, for item: WallpaperItem) {
        for renderer in renderers.values {
            renderer.setUserProperty(key, value: value, for: item)
        }
    }

    private func screenKey(_ screen: NSScreen) -> String {
        let number = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber
        return number.map { "\($0.intValue)" } ?? UUID().uuidString
    }
}
