import SwiftUI

struct WallpaperCardView: View {
    let item: WallpaperItem
    @EnvironmentObject var state: AppState
    @State private var thumbnail: NSImage?

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            ZStack {
                if let thumbnail = thumbnail {
                    Image(nsImage: thumbnail)
                        .resizable()
                        .aspectRatio(contentMode: .fill)
                } else {
                    Color.gray.opacity(0.25)
                    ProgressView()
                }
            }
            .aspectRatio(16 / 9, contentMode: .fit)
            .clipped()
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(state.currentWallpaper?.id == item.id ? Color.accentColor : Color.clear, lineWidth: 3)
            )

            Text(item.title)
                .font(.caption)
                .lineLimit(1)

            HStack {
                Text(item.type.displayName)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                Spacer()
                if !item.tags.isEmpty {
                    Text(item.tags.prefix(2).joined(separator: ", "))
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
            }
        }
        .padding(8)
        .background(Color(nsColor: .controlBackgroundColor))
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .shadow(radius: 2)
        .task { await loadThumbnail() }
    }

    private func loadThumbnail() async {
        guard let url = item.previewURL else { return }
        let image = await Task.detached(priority: .userInitiated) {
            NSImage(contentsOf: url)
        }.value
        if let image = image {
            thumbnail = image
        }
    }
}
