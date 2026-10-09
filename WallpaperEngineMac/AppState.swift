import AppKit
import Combine
import SwiftUI

@MainActor
final class AppState: ObservableObject {
    @Published var currentWallpaper: WallpaperItem?
    @Published var isPaused = false
    @Published var isScanning = false
    /// Bumped on every wallpaper-property change so
    /// views re-evaluate conditional settings.
    @Published private(set) var propertyRevision = 0

    @Published var settings = AppSettings()
    @Published var library = WallpaperLibrary()

    private let windowManager = DesktopWindowManager.shared
    private let pausePolicy = PausePolicy()
    private let powerMonitor = PowerMonitor()
    private let mouseEventTap = MouseEventTap()

    private var galleryWindowController: GalleryWindowController?
    private var settingsWindowController: SettingsWindowController?
    private var previewControllers: [PreviewWindowController] = []
    private var cancellables: Set<AnyCancellable> = []

    func start() {
        // settings and library are nested ObservableObjects;
        // forward their changes so views observing AppState
        // re-render when filters, search text or settings change
        // (otherwise only a full rescan triggers an update).
        settings.objectWillChange
            .sink { [weak self] _ in self?.objectWillChange.send() }
            .store(in: &cancellables)
        library.objectWillChange
            .sink { [weak self] _ in self?.objectWillChange.send() }
            .store(in: &cancellables)

        library.setSteamRoots(steamRoots())
        Task { await rescan() }

        // Debug switch hook: WEM_DEBUG_SWITCH="idA:idB:...:seconds"
        // applies the wallpapers in order, each after
        // the delay — reproduces wallpaper switches
        // (unload + load) outside the gallery, for
        // diagnosing hangs. WEM_DEBUG_GALLERY=1 also
        // opens the gallery window, like a real click.
        if ProcessInfo.processInfo.environment["WEM_DEBUG_GALLERY"] != nil {
            openGallery()
        }
        if let spec = ProcessInfo.processInfo.environment["WEM_DEBUG_SWITCH"] {
            let parts = spec.split(separator: ":")
            if parts.count >= 3,
               let delay = Double(parts[parts.count - 1]) {
                let ids = parts.dropLast().map(String.init)
                for (index, id) in ids.enumerated() {
                    applyDebugWallpaper(
                        id, delay: 1.5 + Double(index) * delay)
                }
            }
        }

        NotificationCenter.default.addObserver(
            forName: NSApplication.didChangeScreenParametersNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.handleScreenChange()
        }

        NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didActivateApplicationNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.evaluatePause()
        }

        pausePolicy.onChange = { [weak self] paused in
            fputs("WEM: pausePolicy onChange paused=\(paused)\n", stderr)
            self?.isPaused = paused
            if paused {
                self?.windowManager.pauseAll()
            } else {
                self?.windowManager.resumeAll()
            }
        }

        powerMonitor.onChange = { [weak self] onBattery, percent in
            self?.handlePowerChange(onBattery: onBattery, percent: percent)
        }

        mouseEventTap.onEvent = { [weak self] event in
            Task { @MainActor in
                self?.windowManager.handleMouseEvent(event)
            }
        }
        if settings.mouseInput {
            fputs("WEM: mouseInput enabled at startup\n", stderr)
            if !mouseEventTap.start() {
                mouseEventTap.ensureAccessibility(prompt: true)
            } else {
                fputs("WEM: mouse event tap active\n", stderr)
            }
        }

        // The event tap cannot be created without the
        // Accessibility grant, and every rebuild resets
        // that grant (the ad-hoc signature changes).
        // The usual flow is: toggle Mouse Input, grant
        // it in System Settings, switch back to the
        // app — retry the tap then, instead of
        // waiting for the user to toggle again.
        NotificationCenter.default.addObserver(
            forName: NSApplication.didBecomeActiveNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.retryMouseEventTap()
        }

        evaluatePause()
    }

    /// Re-creates the mouse event tap if Mouse Input
    /// is on but the tap is missing. That happens
    /// after every rebuild, because the ad-hoc
    /// signature changes and macOS resets the
    /// Accessibility grant: the user typically
    /// grants it in System Settings and switches
    /// back to the app, so retry (and prompt
    /// again if it is still missing) then.
    func retryMouseEventTap() {
        guard settings.mouseInput else { return }
        if mouseEventTap.start() {
            fputs("WEM: mouse event tap active\n", stderr)
        } else {
            mouseEventTap.ensureAccessibility(prompt: true)
        }
    }

    var currentPauseMode: PauseMode {
        PauseMode(rawValue: settings.pauseMode) ?? .focusOrMax
    }

    var currentDisplayMode: DisplayMode {
        DisplayMode(rawValue: settings.displayMode) ?? .crop
    }

    func steamRoots() -> [URL] {
        if !settings.steamPath.isEmpty {
            let url = URL(fileURLWithPath: (settings.steamPath as NSString).expandingTildeInPath)
            if FileManager.default.fileExists(atPath: url.path) {
                return [url]
            }
        }
        return SteamPathManager.candidateSteamDirectories().filter {
            FileManager.default.fileExists(atPath: $0.path)
        }
    }

    func rescan() async {
        isScanning = true
        library.setSteamRoots(steamRoots())
        await library.scan()
        fputs("WEM: scan done, items=\(library.items.count)\n", stderr)
        isScanning = false
        restoreLastWallpaper()
    }

    func restoreLastWallpaper() {
        fputs("WEM: restore id=\(settings.currentWallpaperId) items=\(library.items.count)\n", stderr)
        guard !settings.currentWallpaperId.isEmpty else { return }
        if let item = library.items.first(where: { $0.id == settings.currentWallpaperId }) {
            fputs("WEM: restore found \(item.title)\n", stderr)
            applyWallpaper(item)
        } else {
            fputs("WEM: restore no match\n", stderr)
        }
    }

    func applyWallpaper(_ item: WallpaperItem) {
        fputs("WEM: applyWallpaper \(item.title)\n", stderr)
        currentWallpaper = item
        settings.currentWallpaperId = item.id
        windowManager.applyToAllScreens(item, settings: settings)
        evaluatePause()
    }

    /// Applies the wallpaper with the given workshop
    /// id once the library has been scanned. Used
    /// by the WEM_DEBUG_SWITCH hook.
    private func applyDebugWallpaper(_ id: String, delay: Double) {
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            guard let self else { return }
            if self.library.items.isEmpty {
                self.applyDebugWallpaper(id, delay: 0.5)
                return
            }
            if let item = self.library.items.first(where: { $0.id == id }) {
                fputs("WEM: debug switch applying \(item.title)\n", stderr)
                self.applyWallpaper(item)
            } else {
                fputs("WEM: debug switch: no item with id \(id)\n", stderr)
            }
        }
    }

    /// Saves a wallpaper user property (its settings
    /// sheet value) and applies it to the live
    /// wallpaper if that wallpaper is on screen.
    func setWallpaperProperty(_ item: WallpaperItem, key: String, value: String) {
        WallpaperPropertyStore.shared.setValue(value, for: item, key: key)
        propertyRevision += 1
        if currentWallpaper?.id == item.id {
            windowManager.setUserProperty(key: key, value: value, for: item)
        }
    }

    func removeWallpaper() {
        currentWallpaper = nil
        settings.currentWallpaperId = ""
        windowManager.applyToAllScreens(nil, settings: settings)
    }

    func togglePause() {
        pausePolicy.setManualPause(!pausePolicy.isPaused)
    }

    func evaluatePause() {
        pausePolicy.evaluateWindows(mode: currentPauseMode)
    }

    func applySettingsChanges() {
        fputs("WEM: applySettingsChanges\n", stderr)
        guard let current = currentWallpaper else { return }
        windowManager.applyToAllScreens(current, settings: settings)
    }

    func setMouseInput(_ enabled: Bool) {
        settings.mouseInput = enabled
        if enabled {
            if !mouseEventTap.start() {
                mouseEventTap.ensureAccessibility(prompt: true)
            }
        } else {
            mouseEventTap.stop()
        }
        applySettingsChanges()
    }

    func randomWallpaper() {
        let candidates = library.filteredItems
        guard !candidates.isEmpty else { return }
        applyWallpaper(candidates.randomElement()!)
    }

    func preview(_ item: WallpaperItem) {
        let controller = PreviewWindowController(item: item, settings: settings)
        controller.onClose = { [weak self] in
            self?.previewControllers.removeAll { $0 === controller }
        }
        previewControllers.append(controller)
        controller.showWindow(nil)
    }

    func openGallery() {
        NSApp.activate(ignoringOtherApps: true)
        if galleryWindowController == nil {
            galleryWindowController = GalleryWindowController(state: self)
        }
        galleryWindowController?.showWindow(nil)
        galleryWindowController?.window?.makeKeyAndOrderFront(nil)
    }

    func openSettings() {
        NSApp.activate(ignoringOtherApps: true)
        if settingsWindowController == nil {
            settingsWindowController = SettingsWindowController(state: self)
        }
        settingsWindowController?.showWindow(nil)
        settingsWindowController?.window?.makeKeyAndOrderFront(nil)
    }

    func chooseSteamPath() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.prompt = "Select"
        panel.message = "Select your Steam library folder (contains steamapps/)"
        if panel.runModal() == .OK, let url = panel.url {
            settings.steamPath = url.path
            Task { await rescan() }
        }
    }

    private func handlePowerChange(onBattery: Bool, percent: Int) {
        let lowBattery = settings.pauseBatteryPercent > 0 && onBattery && percent < settings.pauseBatteryPercent
        let shouldPause = (settings.pauseOnBattery && onBattery) || lowBattery
        pausePolicy.setBatteryPause(shouldPause)
    }

    private func handleScreenChange() {
        fputs("WEM: screen parameters changed\n", stderr)
        windowManager.handleScreenConfigurationChange(settings: settings, current: currentWallpaper)
    }
}
