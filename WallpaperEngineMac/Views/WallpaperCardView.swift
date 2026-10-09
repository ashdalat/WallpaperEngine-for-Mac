import ImageIO
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
            Self.previewImage(at: url)
        }.value
        if let image = image {
            thumbnail = image
        }
    }

    /// Some workshop previews are animated GIFs whose
    /// first frame is black (the animation fades in).
    /// SwiftUI renders only the first frame of an
    /// NSImage, so skip ahead to the first frame that
    /// actually has content.
    private static func previewImage(at url: URL) -> NSImage? {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              CGImageSourceGetCount(source) > 1 else {
            return NSImage(contentsOf: url)
        }
        let count = CGImageSourceGetCount(source)
        for i in 0..<count {
            guard let cg = CGImageSourceCreateImageAtIndex(source, i, nil),
                  frameHasContent(cg) else { continue }
            return NSImage(
                cgImage: cg,
                size: NSSize(width: cg.width, height: cg.height)
            )
        }
        return NSImage(contentsOf: url)
    }

    private static func frameHasContent(_ cg: CGImage) -> Bool {
        let w = cg.width, h = cg.height
        let space = CGColorSpaceCreateDeviceRGB()
        guard let ctx = CGContext(
            data: nil, width: w, height: h,
            bitsPerComponent: 8, bytesPerRow: w * 4,
            space: space,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ), let data = ctx.data else { return true }
        ctx.draw(cg, in: CGRect(x: 0, y: 0, width: w, height: h))
        let bytes = data.bindMemory(to: UInt8.self, capacity: w * h * 4)
        var lit = 0, sampled = 0
        var p = 0
        while p + 3 < w * h * 4 {
            if bytes[p + 3] > 12,
               Int(bytes[p]) + Int(bytes[p + 1]) + Int(bytes[p + 2]) > 32 {
                lit += 1
            }
            sampled += 1
            p += 16 // every 4th pixel is plenty
        }
        return lit * 100 / max(1, sampled) >= 2
    }
}
