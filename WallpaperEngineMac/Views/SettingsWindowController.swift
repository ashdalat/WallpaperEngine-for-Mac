import AppKit
import SwiftUI

@MainActor
final class SettingsWindowController: NSWindowController {
    init(state: AppState) {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 560, height: 520),
            styleMask: [.titled, .closable],
            backing: .buffered,
            defer: false
        )
        window.title = "Wallpaper Engine — Settings"
        window.isReleasedWhenClosed = false
        let hosting = NSHostingController(rootView: SettingsView().environmentObject(state))
        window.contentViewController = hosting
        super.init(window: window)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }
}
