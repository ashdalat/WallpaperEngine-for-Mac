import AppKit
import SwiftUI

@MainActor
final class MenuBarController: NSObject, NSMenuDelegate {
    private let statusItem: NSStatusItem
    private let appState: AppState

    init(appState: AppState) {
        self.appState = appState
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        super.init()
        configure()
    }

    private func configure() {
        if let button = statusItem.button {
            button.image = NSImage(systemSymbolName: "photo.on.rectangle", accessibilityDescription: "Wallpaper Engine")
            button.image?.size = NSSize(width: 18, height: 18)
        }
        statusItem.menu = NSMenu()
        statusItem.menu?.delegate = self
    }

    func menuWillOpen(_ menu: NSMenu) {
        rebuildMenu()
    }

    func rebuildMenu() {
        let menu = NSMenu()

        let header = NSMenuItem(title: "Wallpaper Engine Mac", action: nil, keyEquivalent: "")
        header.isEnabled = false
        menu.addItem(header)

        if let current = appState.currentWallpaper {
            let currentItem = NSMenuItem(title: "Current: \(current.title)", action: nil, keyEquivalent: "")
            currentItem.isEnabled = false
            menu.addItem(currentItem)

            menu.addItem(NSMenuItem(
                title: "Remove Wallpaper",
                action: #selector(removeWallpaper),
                keyEquivalent: ""
            ).withTarget(self))
        }

        menu.addItem(NSMenuItem(
            title: appState.isPaused ? "Resume" : "Pause",
            action: #selector(togglePause),
            keyEquivalent: "p"
        ).withTarget(self))

        menu.addItem(NSMenuItem(
            title: "Random Wallpaper",
            action: #selector(randomWallpaper),
            keyEquivalent: "r"
        ).withTarget(self))

        menu.addItem(.separator())

        let displayModeItem = NSMenuItem(title: "Display Mode", action: nil, keyEquivalent: "")
        let displaySubmenu = NSMenu()
        for mode in DisplayMode.allCases {
            let m = NSMenuItem(title: mode.displayName, action: #selector(setDisplayMode(_:)), keyEquivalent: "")
            m.tag = mode.rawValue
            m.state = (mode.rawValue == appState.settings.displayMode) ? .on : .off
            m.target = self
            displaySubmenu.addItem(m)
        }
        displayModeItem.submenu = displaySubmenu
        menu.addItem(displayModeItem)

        let pauseModeItem = NSMenuItem(title: "Pause Mode", action: nil, keyEquivalent: "")
        let pauseSubmenu = NSMenu()
        for mode in PauseMode.allCases {
            let m = NSMenuItem(title: mode.displayName, action: #selector(setPauseMode(_:)), keyEquivalent: "")
            m.tag = mode.rawValue
            m.state = (mode.rawValue == appState.settings.pauseMode) ? .on : .off
            m.target = self
            pauseSubmenu.addItem(m)
        }
        pauseModeItem.submenu = pauseSubmenu
        menu.addItem(pauseModeItem)

        let muteItem = NSMenuItem(
            title: "Mute Audio",
            action: #selector(toggleMute),
            keyEquivalent: ""
        )
        muteItem.state = appState.settings.muteAudio ? .on : .off
        muteItem.target = self
        menu.addItem(muteItem)

        let volumeItem = NSMenuItem(title: "Volume", action: nil, keyEquivalent: "")
        let slider = NSSlider(frame: NSRect(x: 0, y: 0, width: 180, height: 24))
        slider.doubleValue = appState.settings.volume * 100
        slider.target = self
        slider.action = #selector(volumeChanged(_:))
        volumeItem.view = slider
        menu.addItem(volumeItem)

        let mouseItem = NSMenuItem(
            title: "Mouse Input",
            action: #selector(toggleMouseInput),
            keyEquivalent: ""
        )
        mouseItem.state = appState.settings.mouseInput ? .on : .off
        mouseItem.target = self
        menu.addItem(mouseItem)

        menu.addItem(.separator())

        menu.addItem(NSMenuItem(
            title: "Open Gallery…",
            action: #selector(openGallery),
            keyEquivalent: "g"
        ).withTarget(self))

        menu.addItem(NSMenuItem(
            title: "Import Local Video…",
            action: #selector(importLocalVideo),
            keyEquivalent: "i"
        ).withTarget(self))

        menu.addItem(.separator())

        let screensItem = NSMenuItem(
            title: "Displays: \(NSScreen.screens.count)",
            action: nil,
            keyEquivalent: ""
        )
        screensItem.isEnabled = false
        menu.addItem(screensItem)

        menu.addItem(NSMenuItem(
            title: "Quit Wallpaper Engine",
            action: #selector(NSApplication.terminate(_:)),
            keyEquivalent: "q"
        ))

        statusItem.menu = menu
    }

    @objc private func togglePause() {
        appState.togglePause()
    }

    @objc private func randomWallpaper() {
        appState.randomWallpaper()
    }

    @objc private func removeWallpaper() {
        appState.removeWallpaper()
    }

    @objc private func setDisplayMode(_ sender: NSMenuItem) {
        appState.settings.displayMode = sender.tag
        appState.applySettingsChanges()
    }

    @objc private func setPauseMode(_ sender: NSMenuItem) {
        appState.settings.pauseMode = sender.tag
        appState.evaluatePause()
    }

    @objc private func toggleMute() {
        appState.settings.muteAudio.toggle()
        appState.applySettingsChanges()
    }

    @objc private func volumeChanged(_ sender: NSSlider) {
        appState.settings.volume = sender.doubleValue / 100.0
        appState.applySettingsChanges()
    }

    @objc private func toggleMouseInput() {
        appState.setMouseInput(!appState.settings.mouseInput)
    }

    @objc private func openGallery() {
        appState.openGallery()
    }

    @objc private func importLocalVideo() {
        appState.importLocalVideo()
    }

    @objc private func openSettings() {
        appState.openSettings()
    }
}

extension NSMenuItem {
    func withTarget(_ target: AnyObject) -> NSMenuItem {
        self.target = target
        return self
    }
}
