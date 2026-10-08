import AppKit
import SwiftUI

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    let state = AppState()
    private var menuBarController: MenuBarController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        fputs("WEM: didFinishLaunching\n", stderr)
        state.start()
        fputs("WEM: state started\n", stderr)
        menuBarController = MenuBarController(appState: state)
        fputs("WEM: menu bar controller ready\n", stderr)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        false
    }

    func applicationWillTerminate(_ notification: Notification) {
        DesktopWindowManager.shared.applyToAllScreens(nil, settings: state.settings)
    }
}
