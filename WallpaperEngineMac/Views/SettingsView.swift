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
                Text("A Steam library folder (contains steamapps/), or any folder of wallpapers — copy it anywhere and pick it here.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Section("Playback") {
                Picker("Display Mode", selection: $state.settings.displayMode) {
                    ForEach(DisplayMode.allCases) { mode in
                        Text(mode.displayName).tag(mode.rawValue)
                    }
                }
                .onChange(of: state.settings.displayMode) { state.applySettingsChanges() }

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
                    Text("Made by")
                    Spacer()
                    Text("ZCode").foregroundStyle(.secondary)
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
        .frame(width: 540)
    }
}
