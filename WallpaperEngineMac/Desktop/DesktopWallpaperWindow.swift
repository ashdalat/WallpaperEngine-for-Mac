import AppKit

final class DesktopWallpaperWindow: NSWindow {
    let targetScreen: NSScreen

    init(screen: NSScreen) {
        self.targetScreen = screen
        super.init(
            contentRect: screen.frame,
            styleMask: .borderless,
            backing: .buffered,
            defer: false
        )
        configure()
    }

    private func configure() {
        // One level above the desktop picture, just below the
        // desktop icons. At CGWindowLevelForKey(.desktopWindow)
        // the wallpaper shares its level with the macOS desktop
        // picture itself, so closing or minimizing another of
        // our windows made WindowServer reorder that level and
        // the desktop background flashed through for a beat.
        // This slot is otherwise empty, so nothing competes.
        level = NSWindow.Level(Int(CGWindowLevelForKey(.desktopIconWindow)) - 1)
        // .ignoresCycle keeps the window out of the app's
        // window cycling/ordering during those transitions;
        // .fullScreenAuxiliary keeps it put across full-screen
        // space switches.
        collectionBehavior = [.canJoinAllSpaces, .stationary, .ignoresCycle, .fullScreenAuxiliary]
        isOpaque = true
        backgroundColor = .black
        hasShadow = false
        ignoresMouseEvents = true
        isMovableByWindowBackground = false
        isReleasedWhenClosed = false
        isRestorable = false
        hidesOnDeactivate = false
        animationBehavior = .none
        setFrame(targetScreen.frame, display: false)
    }

    func updateFrame() {
        setFrame(targetScreen.frame, display: true)
    }
}
