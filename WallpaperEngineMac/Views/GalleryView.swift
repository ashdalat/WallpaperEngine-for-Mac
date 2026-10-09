import SwiftUI

struct GalleryView: View {
    @EnvironmentObject var state: AppState

    /// The wallpaper whose settings show in the
    /// right-hand pane. Single-click selects;
    /// double-click applies.
    @State private var selectedItem: WallpaperItem?

    private let columns = [
        GridItem(.adaptive(minimum: 220), spacing: 16)
    ]

    var body: some View {
        VStack(spacing: 0) {
            toolbar
            Divider()
            if state.library.items.isEmpty && !state.isScanning {
                emptyState
            } else {
                // Two panes: the wallpaper grid on
                // the left, the settings & info of
                // the selected wallpaper on the
                // right. (A plain HStack, not an
                // HSplitView: HSplitView collapses
                // the right pane to zero width when
                // the left is a ScrollView.)
                HStack(spacing: 0) {
                    ScrollView {
                        LazyVGrid(columns: columns, spacing: 16) {
                            ForEach(state.library.filteredItems) { item in
                                WallpaperCardView(
                                    item: item,
                                    isSelected: selectedItem?.id == item.id
                                )
                                .onTapGesture {
                                    selectedItem = item
                                }
                                .onTapGesture(count: 2) {
                                    state.applyWallpaper(item)
                                }
                                .contextMenu {
                                    Button("Preview") { state.preview(item) }
                                    Button("Open in Finder") {
                                        NSWorkspace.shared.selectFile(nil, inFileViewerRootedAtPath: item.folderURL.path)
                                    }
                                    if state.currentWallpaper?.id == item.id {
                                        Button("Remove Wallpaper") { state.removeWallpaper() }
                                    }
                                }
                            }
                        }
                        .padding()
                    }
                    .frame(minWidth: 300, maxWidth: .infinity)

                    Divider()

                    rightPane
                        .frame(minWidth: 300, idealWidth: 340, maxWidth: 420)
                }
            }
        }
        .frame(minWidth: 760, minHeight: 520)
        .onAppear {
            fputs("WEM: gallery onAppear\n", stderr)
            if state.library.items.isEmpty {
                Task { await state.rescan() }
            }
            applyDebugSelection()
        }
    }

    /// Debug hook (WEM_DEBUG_SELECT=id):
    /// select the wallpaper with that id in
    /// the gallery once the library has loaded
    /// — the same thing a single click on its
    /// card does, which renders the settings
    /// pane. Used to reproduce settings-pane
    /// layout problems without clicking.
    private func applyDebugSelection() {
        guard let id = ProcessInfo.processInfo.environment["WEM_DEBUG_SELECT"],
              !id.isEmpty else { return }
        fputs("WEM: debug select armed for \(id)\n", stderr)
        // GalleryView is a struct, so the
        // closure captures a copy — no weak.
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [self] in
            if state.library.items.isEmpty {
                applyDebugSelection()
                return
            }
            guard let item = state.library.items.first(where: { $0.id == id }) else {
                fputs("WEM: debug select: no item with id \(id)\n", stderr)
                return
            }
            fputs("WEM: debug select \(item.title)\n", stderr)
            selectedItem = item
        }
    }

    /// The settings & info of the selected
    /// wallpaper — the right-hand pane.
    @ViewBuilder
    private var rightPane: some View {
        if let selectedItem {
            WallpaperPropertiesView(item: selectedItem)
                .environmentObject(state)
        } else {
            Text("Select a wallpaper to see its settings")
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    private var toolbar: some View {
        HStack(spacing: 12) {
            Image(systemName: "photo.on.rectangle")
                .font(.title2)
            Text("Wallpaper Engine")
                .font(.headline)
            if state.isScanning {
                ProgressView()
                    .controlSize(.small)
            }
            Spacer()
            TextField("Search", text: $state.library.searchText)
                .textFieldStyle(.roundedBorder)
                .frame(width: 200)
            Button(action: { state.importLocalVideo() }) {
                Label("Import Video", systemImage: "film")
            }
            .help("Import a video from this Mac as a wallpaper")
            ForEach(WallpaperType.allCases.filter { $0 != .unknown }, id: \.self) { type in
                Button(action: {
                    if state.library.enabledTypes.contains(type) {
                        state.library.enabledTypes.remove(type)
                    } else {
                        state.library.enabledTypes.insert(type)
                    }
                }) {
                    Text(type.displayName)
                        .padding(.horizontal, 10)
                        .padding(.vertical, 4)
                        .background(
                            state.library.enabledTypes.contains(type)
                                ? Color.accentColor.opacity(0.25)
                                : Color.gray.opacity(0.15)
                        )
                        .clipShape(Capsule())
                }
                .buttonStyle(.plain)
            }
            Button(action: { Task { await state.rescan() } }) {
                Image(systemName: "arrow.clockwise")
            }
            .help("Rescan library")
            if let current = state.currentWallpaper {
                Button(action: { selectedItem = current }) {
                    Image(systemName: "slider.horizontal.3")
                }
                .help("Show settings for “\(current.title)”")
            }
        }
        .padding()
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Image(systemName: "folder.badge.questionmark")
                .font(.system(size: 48))
                .foregroundStyle(.secondary)
            Text("No wallpapers found")
                .font(.title2)
            Text("Point the app to your Steam library folder containing steamapps/workshop/content/431960")
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            Button("Choose Steam Library…") {
                state.chooseSteamPath()
            }
            .keyboardShortcut(.defaultAction)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding()
    }
}
