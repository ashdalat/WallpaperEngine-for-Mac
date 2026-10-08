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
        level = NSWindow.Level(Int(CGWindowLevelForKey(.desktopWindow)))
        collectionBehavior = [.canJoinAllSpaces, .stationary]
        isOpaque = false
        backgroundColor = .clear
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
