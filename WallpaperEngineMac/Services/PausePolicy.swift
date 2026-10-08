import AppKit
import CoreGraphics

final class PausePolicy {
    private(set) var isPaused: Bool = false
    private var manualPause = false
    private var batteryPause = false
    private var windowPause = false

    // Window-driven pause changes are debounced: a new
    // window state must stay stable for this long before
    // it reaches the renderer. Minimize/maximize
    // ANIMATIONS otherwise flip the state mid-flight (a
    // window briefly passes through near-maximized bounds
    // while zooming, or stays on screen while shrinking
    // into the Dock), which abruptly pauses/resumes the
    // renderer and drops wallpaper frames.
    private static let windowDebounce: TimeInterval = 0.5
    private var pendingWindowPause: Bool?
    private var debounceWork: DispatchWorkItem?

    var onChange: ((Bool) -> Void)?

    func setManualPause(_ paused: Bool) {
        manualPause = paused
        update()
    }

    func setBatteryPause(_ paused: Bool) {
        batteryPause = paused
        update()
    }

    func evaluateWindows(mode: PauseMode) {
        let raw = computeWindowPause(mode: mode)
        if raw == windowPause {
            cancelDebounce()
            return
        }
        if raw {
            // Pausing is debounced: a window entering a
            // near-maximized state mid-animation (minimize/
            // maximize zoom) must settle before the renderer
            // pauses, so window animations do not abruptly
            // drop wallpaper frames.
            scheduleDebounce()
            // Set AFTER scheduling: scheduleDebounce's
            // cancelDebounce() clears pendingWindowPause.
            pendingWindowPause = raw
        } else {
            // Unpausing is immediate. The moment the
            // desktop is visible again (the frontmost
            // window closed or minimized), the renderer
            // must resume presenting to repaint the
            // CAMetalLayer — a delayed resume leaves a
            // black gap on screen.
            cancelDebounce()
            windowPause = false
            update()
        }
    }

    private func scheduleDebounce() {
        cancelDebounce()
        let work = DispatchWorkItem { [weak self] in
            guard let self, let pending = self.pendingWindowPause else { return }
            self.pendingWindowPause = nil
            self.debounceWork = nil
            self.windowPause = pending
            self.update()
        }
        debounceWork = work
        DispatchQueue.main.asyncAfter(
            deadline: .now() + Self.windowDebounce,
            execute: work
        )
    }

    private func cancelDebounce() {
        debounceWork?.cancel()
        debounceWork = nil
        pendingWindowPause = nil
    }

    private func update() {
        let paused = manualPause || batteryPause || windowPause
        if paused != isPaused {
            isPaused = paused
            onChange?(paused)
        }
    }

    private func computeWindowPause(mode: PauseMode) -> Bool {
        switch mode {
        case .never:
            return false
        case .any:
            return !visibleWindows().isEmpty
        case .max:
            return visibleWindows().contains { $0.isMaximized }
        case .focus:
            return frontmostIsNotFinder()
        case .focusOrMax:
            return frontmostIsNotFinder() || visibleWindows().contains { $0.isMaximized }
        case .fullScreen:
            return visibleWindows().contains { $0.isFullScreen }
        }
    }

    private func frontmostIsNotFinder() -> Bool {
        NSWorkspace.shared.frontmostApplication?.bundleIdentifier != "com.apple.finder"
    }

    private struct WindowInfo {
        let owner: String
        let bounds: CGRect

        var isMaximized: Bool {
            guard let screen = bestScreen else { return false }
            let screenFrame = screen.visibleFrame
            return abs(bounds.width - screenFrame.width) < 24 &&
                abs(bounds.height - screenFrame.height) < 24
        }

        var isFullScreen: Bool {
            guard let screen = bestScreen else { return false }
            let frame = screen.frame
            return abs(bounds.width - frame.width) < 2 &&
                abs(bounds.height - frame.height) < 2
        }

        private var bestScreen: NSScreen? {
            NSScreen.screens.min { a, b in
                let da = distance(to: a.frame)
                let db = distance(to: b.frame)
                return da < db
            }
        }

        private func distance(to rect: CGRect) -> CGFloat {
            let dx = max(0, max(rect.minX - bounds.maxX, bounds.minX - rect.maxX))
            let dy = max(0, max(rect.minY - bounds.maxY, bounds.minY - rect.maxY))
            return dx * dx + dy * dy
        }
    }

    private func visibleWindows() -> [WindowInfo] {
        let options: CGWindowListOption = [.optionOnScreenOnly, .excludeDesktopElements]
        guard let list = CGWindowListCopyWindowInfo(options, kCGNullWindowID) as? [[String: Any]] else {
            return []
        }
        let myPID = ProcessInfo.processInfo.processIdentifier
        var result: [WindowInfo] = []
        for entry in list {
            let pid = entry[kCGWindowOwnerPID as String] as? Int32 ?? 0
            if pid == myPID { continue }
            let owner = entry[kCGWindowOwnerName as String] as? String ?? ""
            if owner == "WindowServer" || owner == "Dock" || owner == "SystemUIServer" { continue }
            let layer = entry[kCGWindowLayer as String] as? Int ?? 0
            if layer != 0 { continue }
            var bounds = CGRect.null
            if let b = entry[kCGWindowBounds as String] as? [String: CGFloat] {
                bounds = CGRect(dictionaryRepresentation: b as CFDictionary) ?? .null
            }
            guard !bounds.isNull, bounds.width > 0, bounds.height > 0 else { continue }
            result.append(WindowInfo(owner: owner, bounds: bounds))
        }
        return result
    }
}
