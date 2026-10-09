import AppKit
import CoreGraphics

@MainActor
final class WallpaperRenderer: NSObject {
    private(set) var renderedView: NSView?
    private var videoView: DesktopVideoView?
    private var webView: DesktopWebView?
    private var sceneView: DesktopSceneView?
    private(set) var currentItem: WallpaperItem?

    func load(item: WallpaperItem, in window: NSWindow, settings: AppSettings) {
        unload()
        currentItem = item

        let frame = window.contentView?.bounds ?? .zero
        let displayMode = DisplayMode(rawValue: settings.displayMode) ?? .crop

        switch item.type {
        case .video:
            guard let url = item.contentURL else {
                renderedView = makePlaceholderView(message: "Video file not found.")
                break
            }
            let vv = DesktopVideoView(frame: frame)
            vv.autoresizingMask = [.width, .height]
            vv.load(url: url, displayMode: displayMode, volume: settings.volume, muted: settings.muteAudio)
            videoView = vv
            renderedView = vv
        case .web:
            let wv = DesktopWebView(frame: frame)
            wv.autoresizingMask = [.width, .height]
            wv.load(item: item, mouseEnabled: settings.mouseInput)
            webView = wv
            renderedView = wv
        case .scene:
            guard item.contentURL != nil else {
                renderedView = makePlaceholderView(message: "Scene file not found.")
                break
            }
            let dirs = sceneDirectories(settings: settings)
            let sv = DesktopSceneView(frame: frame)
            sv.autoresizingMask = [.width, .height]
            sv.setMouseEnabled(settings.mouseInput)
            sv.load(item: item, assetsURL: dirs.assets, cacheURL: dirs.cache, settings: settings)
            // applyToAllScreens replaces renderers, so
            // saved user property values must be pushed
            // again here. The engine's message loop is
            // FIFO: the scene load posted by sv.load()
            // is always applied before these.
            let properties = item.project?.properties ?? [:]
            for (key, property) in properties {
                let value = WallpaperPropertyStore.shared.value(
                    for: item, key: key, property: property)
                sv.setUserProperty(key, value: value)
            }
            sceneView = sv
            renderedView = sv
        case .unknown:
            renderedView = makePlaceholderView(
                message: "This wallpaper type is not supported on macOS yet."
            )
        }

        if let renderedView {
            window.contentView?.addSubview(renderedView)
        }
    }

    func unload() {
        videoView?.stop()
        videoView?.removeFromSuperview()
        videoView = nil
        webView?.removeFromSuperview()
        webView = nil
        sceneView?.removeFromSuperview()
        sceneView = nil
        renderedView?.removeFromSuperview()
        renderedView = nil
        currentItem = nil
    }

    func play() {
        videoView?.play()
        webView?.play()
        sceneView?.play()
    }

    func pause() {
        videoView?.pause()
        webView?.pause()
        sceneView?.pause()
    }

    func updateSettings(_ settings: AppSettings) {
        let displayMode = DisplayMode(rawValue: settings.displayMode) ?? .crop
        videoView?.updateDisplayMode(displayMode)
        videoView?.updateVolume(settings.volume, muted: settings.muteAudio)
        webView?.setMouseEnabled(settings.mouseInput)
        sceneView?.setMouseEnabled(settings.mouseInput)
        sceneView?.updateSettings(settings)
    }

    func handleMouseEvent(_ event: CGEvent, window: NSWindow) {
        webView?.handleMouseEvent(event, window: window)
        sceneView?.handleMouseEvent(event, window: window)
    }

    /// Applies one user property change to the running
    /// wallpaper. Only the renderer for `item` is
    /// touched, so a settings edit on one wallpaper
    /// cannot disturb another.
    func setUserProperty(_ key: String, value: String, for item: WallpaperItem) {
        guard currentItem?.id == item.id else { return }
        switch item.type {
        case .scene:
            sceneView?.setUserProperty(key, value: value)
        case .web:
            guard let property = item.project?.properties?[key] else { return }
            webView?.setUserPropertyValues(
                [key: value],
                types: [key: property.type ?? "text"])
        case .video, .unknown:
            break
        }
    }

    // MARK: - Scene wallpaper support

    /// Directories the scene renderer needs: the shared Wallpaper
    /// Engine assets directory and a cache directory.
    private func sceneDirectories(settings: AppSettings) -> (assets: URL, cache: URL) {
        let fm = FileManager.default
        let appSupport = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("WallpaperEngineMac")

        // Many scene wallpapers are fully self-contained in their
        // scene.pkg and work with an empty assets directory; the
        // ones that reference shared Wallpaper Engine models need
        // the real directory (see CHUYEN_MAC.md).
        let assets: URL
        if let shared = sceneAssetsDirectory(settings: settings) {
            assets = shared
        } else {
            assets = appSupport.appendingPathComponent("assets")
            try? fm.createDirectory(at: assets, withIntermediateDirectories: true)
        }

        let cache = fm.urls(for: .cachesDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("WallpaperEngineMac/scene")
        try? fm.createDirectory(at: cache, withIntermediateDirectories: true)
        return (assets, cache)
    }

    private func sceneAssetsDirectory(settings: AppSettings) -> URL? {
        let fm = FileManager.default
        let roots: [URL]
        if !settings.steamPath.isEmpty {
            let url = URL(fileURLWithPath: (settings.steamPath as NSString).expandingTildeInPath)
            roots = fm.fileExists(atPath: url.path) ? [url] : []
        } else {
            roots = SteamPathManager.candidateSteamDirectories().filter { fm.fileExists(atPath: $0.path) }
        }
        return SteamPathManager.wallpaperEngineAssetsDirectories(steamRoots: roots).first
    }

    private func makePlaceholderView(message: String) -> NSView {
        let view = NSView(frame: .zero)
        view.wantsLayer = true
        view.layer?.backgroundColor = NSColor(calibratedWhite: 0.08, alpha: 0.9).cgColor

        let text = NSTextField(labelWithString: message)
        text.textColor = .secondaryLabelColor
        text.alignment = .center
        text.isBezeled = false
        text.isEditable = false
        text.isSelectable = false
        text.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(text)
        NSLayoutConstraint.activate([
            text.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            text.centerYAnchor.constraint(equalTo: view.centerYAnchor),
            text.widthAnchor.constraint(lessThanOrEqualTo: view.widthAnchor, multiplier: 0.8)
        ])
        return view
    }
}
