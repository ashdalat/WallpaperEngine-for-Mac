import AppKit

@MainActor
final class DesktopWindowManager: NSObject {
    static let shared = DesktopWindowManager()

    private var windows: [String: DesktopWallpaperWindow] = [:]
    /// The renderer whose view is on screen.
    private var visibleRenderers: [String: WallpaperRenderer] = [:]
    /// A cross-type switch in progress: its view is
    /// loaded hidden on top of the visible one and
    /// fades in once the new content renders its
    /// first frame. Same-type switches never land
    /// here — they carry the running view instead.
    private var pendingRenderers: [String: WallpaperRenderer] = [:]

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
            pendingRenderers[key]?.unload()
            pendingRenderers[key] = nil
            visibleRenderers[key]?.unload()
            visibleRenderers[key] = nil
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

        // Discard a pending overlap that never became
        // visible — the user switched again mid-load.
        // Its hidden view goes away; the visible base
        // stays on screen.
        if let superseded = pendingRenderers[key] {
            pendingRenderers[key] = nil
            superseded.unload()
        }
        let visible = visibleRenderers[key]

        // Same-type switch on this screen: hand the
        // running view to the next renderer — the
        // player keeps showing the old frame until the
        // new wallpaper renders (no black gap), the
        // engine reloads into the same swapchain, the
        // web page crossfades on top. Everything
        // happens inside one CATransaction — detach
        // from the old host and re-attach to the new
        // one commit together, no intermediate frame.
        let carriedVideoView: DesktopVideoView?
        let carriedWebView: DesktopWebView?
        let carriedSceneView: DesktopSceneView?
        if item!.type == .video, visible?.currentItem?.type == .video {
            carriedVideoView = visible?.takeVideoView()
            carriedWebView = nil
            carriedSceneView = nil
        } else if item!.type == .web, visible?.currentItem?.type == .web {
            carriedVideoView = nil
            carriedWebView = visible?.takeWebView()
            carriedSceneView = nil
        } else if item!.type == .scene, visible?.currentItem?.type == .scene {
            carriedVideoView = nil
            carriedWebView = nil
            carriedSceneView = visible?.takeSceneView()
        } else {
            carriedVideoView = nil
            carriedWebView = nil
            carriedSceneView = nil
        }

        let renderer = WallpaperRenderer()
        fputs("WEM: apply renderer for \(item!.title) on screen\n", stderr)

        if carriedVideoView != nil || carriedWebView != nil || carriedSceneView != nil {
            visible?.unload()
            visibleRenderers[key] = renderer
            renderer.load(item: item!, in: window, settings: settings,
                          carryingVideoView: carriedVideoView,
                          carryingWebView: carriedWebView,
                          carryingSceneView: carriedSceneView)
            // The pause policy only fires onChange on a
            // state transition, so at startup (already
            // unpaused) resumeAll() never runs and the
            // scene — and its audio — never got play().
            // Rendering starts from load(), but the
            // engine's SoundManager only plays on play().
            renderer.play()
        } else if let visible {
            // Cross-type switch: load the new content
            // hidden on top of the visible view; when it
            // renders its first frame, pause the old,
            // start the new, and fade in. The old view
            // never leaves the screen before the new one
            // is actually drawing — no black gap.
            pendingRenderers[key] = renderer
            renderer.load(item: item!, in: window, settings: settings,
                          hideUntilReady: true,
                          onReady: { [weak self, weak visible] in
                guard let self, self.pendingRenderers[key] === renderer else { return }
                self.pendingRenderers[key] = nil
                self.visibleRenderers[key] = renderer
                visible?.pause()
                renderer.play()
                renderer.fadeIn { [weak visible] in
                    visible?.unload()
                }
            })
            // The scene frame timer only runs while
            // playing — a pending scene must play (muted)
            // to ever produce its first frame.
            renderer.startPresentation()
        } else {
            // Nothing on screen (startup, or after a
            // remove) — show immediately; there is no
            // old content to protect.
            visibleRenderers[key] = renderer
            renderer.load(item: item!, in: window, settings: settings)
            renderer.play()
        }

        if let seconds = Double(
            ProcessInfo.processInfo.environment["WEM_DEBUG_BLACKDETECT"] ?? ""),
           seconds > 0 {
            Self.blackDetectToken = UUID()
            Self.blackDetect(window: window, remaining: seconds, token: Self.blackDetectToken)
        }
    }

    /// All live renderers — the visible one plus any
    /// pending overlap.
    private var allRenderers: [WallpaperRenderer] {
        Array(visibleRenderers.values) + Array(pendingRenderers.values)
    }

    func pauseAll() {
        allRenderers.forEach { $0.pause() }
    }

    /// Debug hook (WEM_DEBUG_BLACKDETECT=seconds):
    /// samples the wallpaper window every ~300 ms and
    /// logs whether the frame is uniform — used to
    /// verify switches do not flash a black frame.
    /// Each apply restarts sampling; the token makes
    /// earlier loops stop instead of interleaving.
    private static var blackDetectToken = UUID()

    private static func blackDetect(window: NSWindow, remaining: Double, token: UUID) {
        guard remaining > 0, token == blackDetectToken else { return }
        StaticWallpaper.captureWindow(window) { image in
            DispatchQueue.main.async {
                guard token == blackDetectToken else { return }
                let uniform = image.map(StaticWallpaper.isUniform) ?? true
                fputs("WEM: blackdetect uniform=\(uniform)\n", stderr)
                blackDetect(window: window, remaining: remaining - 0.3, token: token)
            }
        }
    }

    func resumeAll() {
        allRenderers.forEach { $0.play() }
    }

    /// Pushes settings to the live renderers
    /// without reloading the wallpaper. Used for
    /// display transform changes (rotation, flip,
    /// position) — those are pure layer
    /// operations, so a reload's black gap and
    /// scene restart would be pointless cost.
    func updateSettings(_ settings: AppSettings) {
        for renderer in allRenderers {
            renderer.updateSettings(settings)
        }
    }

    func handleScreenConfigurationChange(settings: AppSettings, current: WallpaperItem?) {
        let activeScreens = Set(NSScreen.screens.map(screenKey))
        let staleKeys = windows.keys.filter { !activeScreens.contains($0) }
        for key in staleKeys {
            pendingRenderers[key]?.unload()
            pendingRenderers[key] = nil
            visibleRenderers[key]?.unload()
            visibleRenderers[key] = nil
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
        for (key, renderer) in visibleRenderers {
            guard let window = windows[key] else { continue }
            renderer.handleMouseEvent(event, window: window)
        }
    }

    /// Forwards a user property change to every screen
    /// showing `item` (a wallpaper set to all screens).
    func setUserProperty(key: String, value: String, for item: WallpaperItem) {
        for renderer in allRenderers {
            renderer.setUserProperty(key, value: value, for: item)
        }
    }

    private func screenKey(_ screen: NSScreen) -> String {
        let number = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber
        return number.map { "\($0.intValue)" } ?? UUID().uuidString
    }
}
