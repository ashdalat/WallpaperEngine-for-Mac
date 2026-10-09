import SwiftUI

/// Settings & info panel for one wallpaper — the
/// right-hand pane of the gallery.
///
/// Wallpaper Engine wallpapers describe their own
/// settings in project.json's "properties" table;
/// this view renders a control for each entry
/// (checkbox, slider, color, combo, text, button,
/// texture) and shows the wallpaper's metadata.
/// Changes are saved per wallpaper and applied to
/// the live wallpaper immediately.
struct WallpaperPropertiesView: View {
    let item: WallpaperItem

    @EnvironmentObject var state: AppState

    @State private var showDeleteConfirm = false

    private var properties: [(key: String, property: WEProperty)] {
        guard let properties = item.project?.effectiveProperties else { return [] }
        return properties
            .map { ($0.key, $0.value) }
            .sorted { ($0.property.order ?? 0) < ($1.property.order ?? 0) }
    }

    /// Schemas by key, for evaluating the
    /// visibility conditions of other properties.
    private var propertyMap: [String: WEProperty] {
        Dictionary(properties.map { ($0.key, $0.property) },
                   uniquingKeysWith: { _, last in last })
    }

    var body: some View {
        // Reading the revision makes the pane
        // re-render on every property change, so
        // conditional settings show and hide.
        _ = state.propertyRevision
        return ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                HStack {
                    Text("Settings & Info")
                        .font(.headline)
                    Spacer()
                    if state.isImportedWallpaper(item) {
                        Button("Delete…", role: .destructive) {
                            showDeleteConfirm = true
                        }
                    }
                    Button("Set Wallpaper") {
                        state.applyWallpaper(item)
                    }
                    .keyboardShortcut(.defaultAction)
                }
                .alert("Delete “\(item.title)”?", isPresented: $showDeleteConfirm) {
                    Button("Delete", role: .destructive) {
                        state.deleteWallpaper(item)
                    }
                    Button("Cancel", role: .cancel) {}
                } message: {
                    Text("The video file will be removed from this Mac. This can't be undone.")
                }
                infoSection
                displaySection
                settingsSection
            }
            .padding()
        }
    }

    // MARK: - Info

    private var infoSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .top, spacing: 14) {
                if let previewURL = item.previewURL {
                    AsyncImage(url: previewURL) { phase in
                        switch phase {
                        case .success(let image):
                            image.resizable().aspectRatio(contentMode: .fit)
                        default:
                            previewPlaceholder
                        }
                    }
                    .frame(width: 160, height: 90)
                    .clipShape(RoundedRectangle(cornerRadius: 6))
                } else {
                    previewPlaceholder
                        .frame(width: 160, height: 90)
                }

                VStack(alignment: .leading, spacing: 4) {
                    Text(item.title)
                        .font(.headline)
                    Text(typeDescription)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    if !item.tags.isEmpty {
                        Text(item.tags.joined(separator: ", "))
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    if !item.descriptionText.isEmpty {
                        Text(item.descriptionText)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(4)
                    }
                    Spacer(minLength: 0)
                    Button("Reveal in Finder") {
                        NSWorkspace.shared.selectFile(
                            nil,
                            inFileViewerRootedAtPath: item.folderURL.path)
                    }
                    .buttonStyle(.borderless)
                    .font(.caption)
                }
            }

            GroupBox("Details") {
                LabeledContent("ID", value: item.id)
                if let version = item.project?.version {
                    LabeledContent("Version", value: "\(version)")
                }
                LabeledContent("Content rating", value: item.contentRating)
                LabeledContent("Folder", value: item.folderURL.path(percentEncoded: false))
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
        }
    }

    private var previewPlaceholder: some View {
        RoundedRectangle(cornerRadius: 6)
            .fill(Color.gray.opacity(0.2))
            .overlay(
                Image(systemName: "photo")
                    .foregroundStyle(.secondary)
            )
    }

    private var typeDescription: String {
        let type = item.project?.type ?? item.type.rawValue
        return "\(item.type.displayName) wallpaper · \(type)"
    }

    // MARK: - Display

    /// Display transform options (Waywallen-style
    /// rotation/flip/position). These live with
    /// the wallpaper selection, not in the app
    /// Settings menu. AppState watches UserDefaults
    /// for these keys and applies them to the live
    /// wallpaper instantly — no onChange needed.
    private var displaySection: some View {
        GroupBox("Display") {
            VStack(alignment: .leading, spacing: 10) {
                Picker("Rotation", selection: $state.settings.rotation) {
                    ForEach(DisplayRotation.allCases) { rotation in
                        Text(rotation.displayName).tag(rotation.rawValue)
                    }
                }

                Picker("Flip", selection: $state.settings.flip) {
                    ForEach(DisplayFlip.allCases) { flip in
                        Text(flip.displayName).tag(flip.rawValue)
                    }
                }

                Divider()

                HStack {
                    Text("Position")
                    Spacer()
                    Text("X \(Int(state.settings.offsetX))")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Text("Y \(Int(state.settings.offsetY))")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                HStack {
                    Text("X")
                    Slider(value: $state.settings.offsetX, in: 0...100)
                    Text("Y")
                    Slider(value: $state.settings.offsetY, in: 0...100)
                }
                HStack(spacing: 4) {
                    ForEach(displayAlignments) { alignment in
                        Button(alignment.name) {
                            state.settings.offsetX = alignment.x
                            state.settings.offsetY = alignment.y
                        }
                        .font(.system(size: 11, weight: .semibold))
                        .frame(maxWidth: .infinity)
                    }
                }
            }
            .padding(4)
        }
    }

    // MARK: - Settings

    @ViewBuilder
    private var settingsSection: some View {
        if properties.isEmpty {
            GroupBox("Settings") {
                Text("This wallpaper has no settings in its project.json.")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
        } else {
            GroupBox("Settings") {
                VStack(alignment: .leading, spacing: 10) {
                    ForEach(properties, id: \.key) { entry in
                        if isVisible(entry.key, entry.property) {
                            propertyRow(entry.key, entry.property)
                        }
                    }
                }
                .padding(4)
            }
        }
    }

    @ViewBuilder
    private func propertyRow(_ key: String, _ property: WEProperty) -> some View {
        switch property.control {
        case .checkbox:
            HStack {
                Text(property.label)
                Spacer()
                Toggle(isOn: boolBinding(key, property)) {
                    Text(property.value.map { $0 == "true" ? "On" : "Off" } ?? "Off")
                        .foregroundStyle(.secondary)
                }
                .labelsHidden()
                .toggleStyle(.checkbox)
                .frame(width: 60, alignment: .trailing)
            }
        case .slider:
            VStack(alignment: .leading, spacing: 2) {
                HStack {
                    Text(property.label)
                    Spacer()
                    Text(formatSlider(sliderValue(key, property)))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                if sliderTickCount(property) <= 1000 {
                    Slider(value: sliderBinding(key, property),
                           in: sliderRange(property),
                           step: sliderStep(property))
                } else {
                    // A discrete SwiftUI slider draws
                    // one tick mark per step. With the
                    // tiny steps some wallpapers use
                    // (0...180 at 0.001 = 180,000
                    // marks) the slider's tick-mark
                    // layout never converges and the
                    // window spins at 100% CPU, so
                    // those render as continuous.
                    Slider(value: sliderBinding(key, property),
                           in: sliderRange(property))
                }
            }
        case .color:
            HStack {
                Text(property.label)
                Spacer()
                ColorPicker(selection: colorBinding(key, property),
                            supportsOpacity: true) {
                    Text("Color")
                }
                .frame(width: 60)
                .labelsHidden()
            }
        case .combo:
            if !(property.options ?? []).isEmpty {
                HStack {
                    Text(property.label)
                    Spacer()
                    Picker(selection: stringBinding(key, property),
                           label: Text(property.label)) {
                        ForEach(Array((property.options ?? []).enumerated()),
                                id: \.offset) { _, option in
                            Text(option.displayText)
                                .tag(option.value ?? "")
                        }
                    }
                    .frame(width: 180)
                }
            } else {
                textRow(key, property)
            }
        case .button:
            HStack {
                Text(property.label)
                Spacer()
                Button("Trigger") {
                    // Wallpaper Engine button properties fire
                    // their event with a "true" value.
                    state.setWallpaperProperty(item, key: key, value: "true")
                }
            }
        case .texture:
            HStack {
                Text(property.label)
                Spacer()
                TextField("Path", text: stringBinding(key, property))
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 200)
                Button("Browse…") {
                    browseTexture(key, property)
                }
            }
        case .group:
            // The scene editor's group entries are
            // section headers, not controls.
            Text(property.plainText)
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(.secondary)
                .padding(.top, 6)
        case .text:
            if property.value?.isEmpty ?? true {
                // "text" properties without a value
                // are author notes, not inputs.
                Text(property.plainText)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            } else {
                textRow(key, property)
            }
        case .unsupported:
            HStack {
                Text(property.label)
                Spacer()
                Text(currentValue(key, property))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private func textRow(_ key: String, _ property: WEProperty) -> some View {
        HStack {
            Text(property.label)
            Spacer()
            TextField(property.label, text: stringBinding(key, property))
                .textFieldStyle(.roundedBorder)
                .frame(width: 200)
        }
    }

    /// Wallpaper Engine only shows a setting once
    /// its condition holds, e.g. "shu.value==true"
    /// or a bare "shu.value" (true when the
    /// referenced checkbox is on).
    private func isVisible(_ key: String,
                           _ property: WEProperty) -> Bool {
        guard let condition = property.condition?
                .trimmingCharacters(in: .whitespacesAndNewlines),
              !condition.isEmpty else { return true }
        guard let dot = condition.firstIndex(of: ".") else { return true }
        let depKey = String(condition[..<dot])
        var rest = String(condition[condition.index(after: dot)...])
        guard rest.hasPrefix("value") else { return true }
        rest = String(rest.dropFirst("value".count))
        guard let depProperty = propertyMap[depKey] else { return true }
        let depValue = WallpaperPropertyStore.shared.value(
            for: item, key: depKey, property: depProperty)
        if rest.isEmpty { return depValue == "true" }
        if rest.hasPrefix("==") {
            return depValue == String(rest.dropFirst(2))
        }
        if rest.hasPrefix("!=") {
            return depValue != String(rest.dropFirst(2))
        }
        return true
    }

    // MARK: - Bindings

    private func currentValue(_ key: String, _ property: WEProperty) -> String {
        WallpaperPropertyStore.shared.value(for: item, key: key, property: property)
    }

    private func stringBinding(_ key: String,
                               _ property: WEProperty) -> Binding<String> {
        Binding(
            get: { currentValue(key, property) },
            set: { state.setWallpaperProperty(self.item, key: key, value: $0) }
        )
    }

    private func boolBinding(_ key: String,
                             _ property: WEProperty) -> Binding<Bool> {
        Binding(
            get: { currentValue(key, property) == "true" },
            set: { state.setWallpaperProperty(
                self.item, key: key, value: $0 ? "true" : "false") }
        )
    }

    private func sliderRange(_ property: WEProperty) -> ClosedRange<Double> {
        let min = property.min ?? 0
        let max = property.max ?? 1
        return min <= max ? min...max : 0...1
    }

    private func sliderStep(_ property: WEProperty) -> Double {
        property.step ?? 0.01
    }

    /// How many discrete steps the slider's
    /// range holds — i.e. how many tick marks
    /// a discrete SwiftUI slider would draw.
    private func sliderTickCount(_ property: WEProperty) -> Int {
        let min = property.min ?? 0
        let max = property.max ?? 1
        let step = property.step ?? 0.01
        guard step > 0, max > min else { return 0 }
        return Int((max - min) / step)
    }

    private func sliderValue(_ key: String, _ property: WEProperty) -> Double {
        currentValue(key, property).doubleValue ?? (property.min ?? 0)
    }

    private func sliderBinding(_ key: String,
                               _ property: WEProperty) -> Binding<Double> {
        Binding(
            get: { self.sliderValue(key, property) },
            set: { state.setWallpaperProperty(self.item, key: key, value: String($0)) }
        )
    }

    private func colorBinding(_ key: String,
                              _ property: WEProperty) -> Binding<Color> {
        Binding(
            get: { parseColor(self.currentValue(key, property)) },
            set: {
                let value = colorString($0)
                state.setWallpaperProperty(self.item, key: key, value: value)
            }
        )
    }

    private func browseTexture(_ key: String, _ property: WEProperty) {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Select"
        panel.message = "Choose an image for this property"
        if panel.runModal() == .OK, let url = panel.url {
            state.setWallpaperProperty(item, key: key, value: url.path)
        }
    }

    // MARK: - Value formatting

    private func formatSlider(_ value: Double) -> String {
        if value == value.rounded() && abs(value) < 1_000_000 {
            return String(format: "%.0f", value)
        }
        return String(format: "%.3g", value)
    }

    /// Wallpaper Engine colors travel as
    /// "red green blue [alpha]" float strings,
    /// each component 0...1.
    private func parseColor(_ string: String) -> Color {
        let parts = string.split(separator: " ").compactMap { Double($0) }
        let r = parts.count > 0 ? parts[0] : 0
        let g = parts.count > 1 ? parts[1] : 0
        let b = parts.count > 2 ? parts[2] : 0
        let a = parts.count > 3 ? parts[3] : 1
        return Color(red: r, green: g, blue: b, opacity: a)
    }

    private func colorString(_ color: Color) -> String {
        let nsColor = NSColor(color)
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        nsColor.getRed(&r, green: &g, blue: &b, alpha: &a)
        return "\(r) \(g) \(b) \(a)"
    }
}

private extension String {
    var doubleValue: Double? { Double(self) }
}

/// One of the nine Waywallen "Align"
/// positions — a shortcut that sets the
/// X/Y position offset.
private struct DisplayAlignment: Identifiable {
    let name: String
    let x: Double
    let y: Double
    var id: String { name }
}

/// The align grid, reading order
/// top-left to bottom-right.
private let displayAlignments: [DisplayAlignment] = [
    .init(name: "↖", x: 0, y: 0),
    .init(name: "↑", x: 50, y: 0),
    .init(name: "↗", x: 100, y: 0),
    .init(name: "←", x: 0, y: 50),
    .init(name: "•", x: 50, y: 50),
    .init(name: "→", x: 100, y: 50),
    .init(name: "↙", x: 0, y: 100),
    .init(name: "↓", x: 50, y: 100),
    .init(name: "↘", x: 100, y: 100),
]
