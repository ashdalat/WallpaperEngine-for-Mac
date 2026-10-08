import AppKit
import CoreGraphics

final class PausePolicy {
    private(set) var isPaused: Bool = false
    private var manualPause = false
    private var batteryPause = false
    private var windowPause = false

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
        windowPause = computeWindowPause(mode: mode)
        update()
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
