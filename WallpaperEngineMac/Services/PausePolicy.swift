import AppKit
import CoreGraphics

/// Decides when the wallpaper renderer may pause.
///
/// Only an explicit manual pause (menu bar) may stop
/// the renderer. Window- and battery-driven pauses
/// are intentionally ignored: a paused renderer stops
/// presenting, and WindowServer purges the presented
/// CAMetalLayer drawable during window close/minimize
/// animations. With no presents coming, that purge
/// stays visible as a black flash on the desktop
/// (and stays black until the next resume if another
/// app becomes frontmost).
///
/// Keeping the renderer presenting at all
/// times repairs any purge within one
/// frame (~33 ms at 30 fps) so it never
/// becomes visible. We do the same.
final class PausePolicy {
    private(set) var isPaused: Bool = false
    private var manualPause = false

    var onChange: ((Bool) -> Void)?

    func setManualPause(_ paused: Bool) {
        manualPause = paused
        update()
    }

    func setBatteryPause(_ paused: Bool) {
        fputs("WEM: pausePolicy battery pause=\(paused) ignored\n", stderr)
    }

    func evaluateWindows(mode: PauseMode) {
        fputs("WEM: pausePolicy evaluate mode=\(mode.rawValue) ignored\n", stderr)
    }

    private func update() {
        let paused = manualPause
        if paused != isPaused {
            isPaused = paused
            onChange?(paused)
        }
    }
}
