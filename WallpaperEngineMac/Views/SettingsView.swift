import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var state: AppState

    var body: some View {
        Form {
            Section("Library") {
                HStack {
                    TextField("Library path", text: $state.settings.steamPath)
                    Button("Browse…") { state.chooseSteamPath() }
                }
                Button("Rescan Library") {
                    Task { await state.rescan() }
                }
                VStack(alignment: .leading, spacing: 4) {
                    Text("Pick the Steam library folder that holds the wallpapers — the one containing:")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Text("• steamapps/workshop/content/431960 — the Workshop Content (the wallpapers)")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Text("• steamapps/common/wallpaper_engine/assets — Shared Assets (~85 MB), used by scene wallpapers")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Text("Example: ~/Library/Application Support/Steam — or any folder of wallpapers copied anywhere.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Section("Playback") {
                Picker("Display Mode", selection: $state.settings.displayMode) {
                    ForEach(DisplayMode.allCases) { mode in
                        Text(mode.displayName).tag(mode.rawValue)
                    }
                }
                .onChange(of: state.settings.displayMode) { state.applySettingsChanges() }

                Picker("Rotation", selection: $state.settings.rotation) {
                    ForEach(DisplayRotation.allCases) { rotation in
                        Text(rotation.displayName).tag(rotation.rawValue)
                    }
                }
                .onChange(of: state.settings.rotation) { state.applySettingsChanges() }

                Picker("Flip", selection: $state.settings.flip) {
                    ForEach(DisplayFlip.allCases) { flip in
                        Text(flip.displayName).tag(flip.rawValue)
                    }
                }
                .onChange(of: state.settings.flip) { state.applySettingsChanges() }

                Toggle("Mute Audio", isOn: $state.settings.muteAudio)
                    .onChange(of: state.settings.muteAudio) { state.applySettingsChanges() }

                Slider(value: $state.settings.volume, in: 0...1) {
                    Text("Volume")
                }
                .onChange(of: state.settings.volume) { state.applySettingsChanges() }

                Toggle("Mouse Input (Web wallpapers)", isOn: Binding(
                    get: { state.settings.mouseInput },
                    set: { state.setMouseInput($0) }
                ))
            }

            Section("Pause & Power") {
                Picker("Pause Mode", selection: $state.settings.pauseMode) {
                    ForEach(PauseMode.allCases) { mode in
                        Text(mode.displayName).tag(mode.rawValue)
                    }
                }
                .onChange(of: state.settings.pauseMode) { state.evaluatePause() }

                Toggle("Pause when on battery power", isOn: $state.settings.pauseOnBattery)
                Stepper(
                    "Pause when battery is below \(state.settings.pauseBatteryPercent)%",
                    value: $state.settings.pauseBatteryPercent,
                    in: 0...100
                )
            }

            Section("About") {
                HStack {
                    Text("Made using")
                    Spacer()
                    Text("ZCode Ling 3.1 Flash").foregroundStyle(.secondary)
                }
                HStack {
                    Text("Current wallpaper")
                    Spacer()
                    Text(state.currentWallpaper?.title ?? "None").foregroundStyle(.secondary)
                }
                HStack {
                    Text("Displays")
                    Spacer()
                    Text("\(NSScreen.screens.count)").foregroundStyle(.secondary)
                }
                HStack {
                    Text("Wallpapers in library")
                    Spacer()
                    Text("\(state.library.items.count)").foregroundStyle(.secondary)
                }
            }
        }
        .formStyle(.grouped)
        // The hosting controller sizes the
        // window to the content's minimum —
        // a Form's minimum height is zero,
        // which collapsed the window to its
        // title bar. Pin a minimum height so
        // the window keeps a usable size.
        .frame(width: 540)
        .frame(minHeight: 480)
    }
}
