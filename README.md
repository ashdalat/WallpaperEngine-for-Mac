# Wallpaper Engine Mac — Ứng dụng hình nền động NATIVE cho macOS

Ứng dụng macOS native (Swift / AppKit / SwiftUI) cho phép người dùng chọn và sử dụng trực tiếp
các hình nền động từ **Steam Wallpaper Engine** (appId `431960`) trên macOS — hỗ trợ đa màn hình,
tạm dừng thông minh, và tối ưu pin.

---

## 1. PHÂN TÍCH SOURCE CODE THAM KHẢO

### 1.1. Plasma6 Wallpaper Engine Plugin (`plasma6_wallpaper_engine_git_src`)

**Cấu trúc thư mục Wallpaper Engine trên Steam:**

| Đường dẫn | Nội dung |
|---|---|
| `<steam>/steamapps/workshop/content/431960/<workshopId>/` | Wallpaper từ Workshop, mỗi thư mục con = 1 wallpaper |
| `<steam>/steamapps/common/wallpaper_engine/projects/defaultprojects/` | Wallpaper mặc định |
| `<steam>/steamapps/common/wallpaper_engine/projects/myprojects/` | Wallpaper tự tạo |
| `<steam>/steamapps/common/wallpaper_engine/assets/` | Assets cho wallpaper type `scene` |
| `<steam>/steamapps/common/wallpaper_engine/config.json` | Cấu hình toàn cục (playlists, ...) |

**File `project.json` (được parse trong `WallpaperListModel.qml` — hàm `loadItemFromJson`):**

```json
{
    "title": "Frieren",                 // Tiêu đề hiển thị
    "description": "...",               // Mô tả
    "file": "ab66f357dd3a68566342c5570be2f859.mp4",  // File nội dung chính
    "preview": "preview.gif",           // Thumbnail (jpg/gif/png)
    "type": "video",                    // video | web | scene
    "contentrating": "Everyone",        // Everyone | Questionable | Mature
    "tags": ["Anime"],                  // Thẻ phân loại
    "version": 0,
    "general": {                        // General properties (schemecolor, fps...)
        "properties": {
            "schemecolor": { "order": 0, "text": "ui_browse_properties_scheme_color",
                             "type": "color", "value": "0 0 0" }
        }
    },
    "properties": {                     // User properties của web wallpaper
        "myProp": { "order": 0, "text": "...", "type": "text", "value": "..." }
    }
}
```

**Cơ chế render theo loại (`main.qml` — hàm `loadBackend()`):**

- **`video`** → `Mpv.qml` (mpv render qua OpenGL vào QSGTexture, hardware decode) hoặc
  `QtMultimedia.qml`. Hỗ trợ: `displayMode` (Aspect/Crop/Scale), `speed`, `volume`, `mute`, loop.
- **`web`** → `QtWebView.qml` (QtWebEngine). Cơ chế giao tiếp JS↔native qua **QWebChannel**:
  - Object `wpeQml` (WebChannel.id) expose các signal: `sigGeneralProperties`, `sigUserProperties`, `sigAudio`.
  - Inject script vào `MainWorld` để tạo `window.wpeQml`, `window.wallpaperRegisterAudioListener`.
  - Web wallpaper gọi `wallpaperPropertyListener.applyGeneralProperties(props)` /
    `applyUserProperties(props)` — đây là **giao thức chuẩn của Wallpaper Engine** mà app macOS tái tạo
    trong `DesktopWebView` (inject bridge vào `WKWebView`).
  - `settings.localContentCanAccessRemoteUrls = true` để load file local
    (tương đương macOS: `loadFileURL(_:allowingReadAccessTo:)`).
- **`scene`** → `Scene.qml` (SceneViewer — runtime C++ riêng, render scene 3D Unity-like).
  **Không thể port sang macOS** (cần runtime riêng của WE).

**Cơ chế tạm dừng (`WindowModel.qml` + `PowerSource.qml` + `main.qml`):**

- `PauseMode`: `Never`, `Any`, `Max`, `Focus`, `FocusOrMax`, `FullScreen` — duyệt
  `TaskManager.TasksModel` để phát hiện cửa sổ đang focus/maximized/fullscreen.
- `PowerSource`: đọc `powermanagement` engine — pause khi chạy pin (`NoCharge`/`Discharging`)
  hoặc pin dưới ngưỡng `PauseBatPercent`.
- `TTYSwitchMonitor`: pause khi chuyển VT (tương đương macOS: pause khi sleep/lock).
- `MouseGrabber`: hook chuột toàn cục, forward vào web wallpaper (tương đương macOS: CGEventTap).
- Volume fade dần (300ms) khi play/pause để tránh tiếng "pop".

### 1.2. Ứng dụng macOS mẫu (`Dynamic Wallpaper.saver`)

Là một **Screen Saver bundle** (`CFBundlePackageType: BNDL`, `NSPrincipalClass: DongtaiScreenSaverView`).
Cơ chế hiển thị nền desktop trên macOS được tái tạo trong project này bằng AppKit:

```swift
window.level = NSWindow.Level(Int(CGWindowLevelForKey(.desktopWindow)))  // level 0
window.collectionBehavior = [.canJoinAllSpaces, .stationary]
window.isOpaque = false
window.backgroundColor = .clear
window.ignoresMouseEvents = true   // để click xuyên xuống icon desktop
```

Cửa sổ ở `desktopWindow` level (level 0) nằm **phía trên hình nền hệ thống nhưng phía dưới
icon desktop và mọi cửa sổ ứng dụng khác** — đúng hành vi của một wallpaper engine.

---

## 2. KIẾN TRÚC DỰ ÁN (Architecture Plan)

```
WallpaperEngineMac/
├── AppDelegate.swift                  # @main, LSUIElement agent app, khởi tạo MenuBar + AppState
├── AppState.swift                     # Central coordinator (ObservableObject)
├── Models/
│   ├── WEProject.swift                # Codable parser cho project.json (title, file, type, preview, tags, general/properties)
│   ├── WallpaperItem.swift            # Model wallpaper (id, title, type, preview, file)
│   ├── WallpaperType.swift            # enum video/web/scene/unknown
│   ├── SteamPathManager.swift         # Discover Steam library, parse libraryfolders.vdf, WE assets dir
│   ├── WallpaperLibrary.swift         # Async scanner thư mục workshop → [WallpaperItem]
│   └── AppSettings.swift              # @AppStorage settings + PauseMode/DisplayMode enums
├── Desktop/
│   ├── DesktopWallpaperWindow.swift   # NSWindow @ desktopWindow level (level 0)
│   ├── DesktopWindowManager.swift     # 1 window + 1 renderer MỖI MÀN HÌNH (multi-monitor)
│   ├── DesktopVideoView.swift         # AVPlayer + AVPlayerLayer (hardware decode M-series/Intel)
│   ├── DesktopWebView.swift           # WKWebView trong suốt + WE JS bridge + mouse forwarding
│   ├── DesktopSceneView.swift         # NSView + CAMetalLayer cho wallpaper 3D (scene)
│   └── WallpaperRenderer.swift        # Factory: chọn backend theo wallpaper type
├── Scene/
│   ├── SceneRendererBridge.h/.mm      # Obj-C++ bridge bọc wallpaper::SceneWallpaper (engine C++)
│   └── (link scene_backend/build/install/lib/libwescene-renderer.a — Vulkan→Metal qua MoltenVK)
├── Services/
│   ├── PausePolicy.swift              # PauseMode (Never/Any/Max/Focus/FocusOrMax/FullScreen)
│   ├── PowerMonitor.swift             # IOKit.ps — battery state & percent
│   └── MouseEventTap.swift            # CGEventTap listen-only → forward chuột vào WKWebView
├── MenuBar/
│   └── MenuBarController.swift        # NSStatusItem: pause, display mode, volume, screens
└── Views/
    ├── GalleryView.swift              # SwiftUI gallery grid + search + type filter
    ├── WallpaperCardView.swift        # Card với thumbnail (preview.jpg/gif) async load
    ├── GalleryWindowController.swift  # NSWindow hosting GalleryView
    ├── SettingsView.swift             # SwiftUI settings (Steam path, playback, power)
    ├── SettingsWindowController.swift
    └── PreviewWindowController.swift  # Cửa sổ preview nổi (reuse WallpaperRenderer)
```

**Luồng dữ liệu:**

```
Steam Library ──scan──▶ WallpaperLibrary ──▶ GalleryView (SwiftUI grid)
                                                │ tap
                                                ▼
                                        AppState.applyWallpaper(item)
                                                │
                        ┌───────────────────────┴───────────────────────┐
                        ▼                                               ▼
              DesktopWindowManager                          PausePolicy / PowerMonitor
        (per-screen DesktopWallpaperWindow)                 (auto pause/resume)
                        │
                        ▼
              WallpaperRenderer (factory theo type)
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
   DesktopVideoView  DesktopWebView  DesktopSceneView
   (AVPlayerLayer)   (WKWebView)     (CAMetalLayer + MoltenVK)
```

**Ánh xạ tính năng Plasma6 → macOS:**

| Tính năng Plasma6 | macOS implementation | Trạng thái |
|---|---|---|
| `Mpv.qml` / `QtMultimedia.qml` | `DesktopVideoView` — AVPlayer/AVPlayerLayer (VideoToolbox hardware decode trên M1-M4 & Intel) | ✅ |
| `QtWebView.qml` + QWebChannel | `DesktopWebView` — WKWebView + inject `wallpaperPropertyListener` bridge | ✅ |
| `MouseGrabber` | `MouseEventTap` — CGEventTap listen-only, dispatch MouseEvent vào DOM | ✅ |
| `WindowModel` (PauseMode) | `PausePolicy` — CGWindowListCopyWindowInfo + NSWorkspace.frontmostApplication | ✅ |
| `PowerSource` | `PowerMonitor` — IOKit.ps (IOPSCopyPowerSourcesInfo) | ✅ |
| `TTYSwitchMonitor` | NSApplication.didChangeScreenParametersNotification + lifecycle | ✅ |
| `WallpaperListModel` | `WallpaperLibrary` (async Task.detached scan) | ✅ |
| Plasma desktop widget | `DesktopWallpaperWindow` @ `CGWindowLevelForKey(.desktopWindow)` | ✅ |
| Đa màn hình | `DesktopWindowManager` — 1 window/renderer mỗi `NSScreen`, key = CGDisplayID | ✅ |
| `Scene.qml` | `DesktopSceneView` + `SceneRendererBridge` — engine wallpaper-scene-renderer (Vulkan) dịch qua MoltenVK (Metal). Cần build `scene_backend/` trước, xem [§3](#3-build--chạy-thử) | ✅ |

---

## 3. BUILD & CHẠY THỬ

### Yêu cầu
- macOS 12.0+ (Monterey trở lên), Xcode 15+
- Swift 5.9+
- [Homebrew](https://brew.sh) + `brew install xcodegen` (XcodeGen)
- Scene wallpaper (3D): `scene_backend/BuildSceneBackend.sh` tự cài `cmake pkg-config lz4 molten-vk`

### Cách 1: Build tự động (khuyến nghị)

```bash
cd /path/to/WallpaperEngineMac
chmod +x build.sh
./build.sh
# (tự build scene engine trước nếu chưa có:
#  clone wallpaper-scene-renderer → ApplyMoltenVKPatches.sh
#  → BuildSceneBackend.sh → libwescene-renderer.a → xcodegen → xcodebuild)
open build/Build/Products/Debug/WallpaperEngineMac.app
```

### Scene wallpaper (3D) build thủ công

```bash
cd scene_backend
bash BuildSceneBackend.sh
# → build/install/lib/libwescene-renderer.a
# Engine: github.com/catsout/wallpaper-scene-renderer (C++20, Vulkan),
# chạy trên macOS nhờ MoltenVK (Vulkan→Metal).
# ApplyMoltenVKPatches.sh patch các chỗ Linux-only:
# dlopen libvulkan.so → libMoltenVK.dylib, bỏ FD external memory
# extensions (chỉ offscreen path dùng), bật portability extensions.
```

> Scene wallpaper cần thư mục shared assets của Wallpaper Engine:
> `steamapps/common/wallpaper_engine/assets` (copy từ máy Linux sang
> `~/Library/Application Support/Steam/steamapps/common/wallpaper_engine/assets`,
> xem `CHUYEN_MAC.md`). Wallpaper self-contained trong `scene.pkg` vẫn chạy mà không cần nó.

### Cách 2: Tạo project trong Xcode thủ công

1. Xcode → File → New → Project → macOS → App
   - Product Name: `WallpaperEngineMac`, Interface: **SwiftUI**, Language: **Swift**
2. Xoá file `ContentView.swift` và `WallpaperEngineMacApp.swift` mặc định.
3. Kéo toàn bộ thư mục `WallpaperEngineMac/` (trừ `Info.plist` nếu đã có) vào project navigator
   (chọn "Create groups", target `WallpaperEngineMac`).
4. Target → Info tab: thêm `Application is agent (UIElement)` = `YES` (key `LSUIElement`).
   Hoặc dùng file `Info.plist` có sẵn trong project này.
5. Target → Build Settings → Deployment Target: macOS 12.0.
6. Cmd+R để build & run.

### Cấp quyền (cho Mouse Input)
Nếu bật "Mouse Input (Web)", app cần quyền **Accessibility**:
`System Settings → Privacy & Security → Accessibility → thêm WallpaperEngineMac`.
(Chỉ cần cho tính năng tương tác chuột với web wallpaper; video wallpaper không cần.)

### Chạy thử với wallpaper mẫu

```bash
# Tạo thư mục workshop giả lập từ mẫu Frieren có sẵn
mkdir -p ~/Library/Application\ Support/Steam/steamapps/workshop/content/431960/1234567890
cp -R "/đường/dẫn/đến/Frieren_Live_Wallpaper_1080p/"* \
      ~/Library/Application\ Support/Steam/steamapps/workshop/content/431960/1234567890/
```

Hoặc dùng menu **Settings → Browse** để trỏ tới thư mục Steam library của bạn.

Sau đó:
1. Mở **Gallery** từ Menu Bar (icon 🖻).
2. Click vào card wallpaper → wallpaper chạy nền desktop.
3. Menu Bar: Pause/Resume, Remove Wallpaper, Random, Display Mode (Aspect/Crop/Scale),
   Pause Mode, Volume, Mute, Mouse Input (Web).
4. Context menu trên card: Preview (cửa sổ nổi), Open in Finder, Remove Wallpaper.

---

## 4. TROUBLESHOOTING

| Vấn đề | Nguyên nhân / Khắc phục |
|---|---|
| Gallery rỗng | Steam path chưa đúng — dùng Settings → Browse chọn thư mục chứa `steamapps/`. Kiểm tra thư mục `steamapps/workshop/content/431960/<id>/project.json` tồn tại. |
| Wallpaper không hiện trên desktop | Đảm bảo không có app wallpaper khác đang chạy (Dynamic Wallpaper, Wallpaper Engine…). Thử Pause → Resume từ Menu Bar. |
| Video không có tiếng | Menu Bar → bỏ tick "Mute Audio". Video cần codec được VideoToolbox hỗ trợ (H.264/HEVC mp4 luôn chạy; webm VP9/AV1 tuỳ phiên bản macOS). |
| Chuột không tương tác web wallpaper | Bật "Mouse Input (Web)" trong Settings, sau đó cấp quyền **Accessibility** cho app trong System Settings → Privacy & Security. |
| Web wallpaper nền đen | Một số web wallpaper tự set background — app đã inject CSS `html, body { background: transparent }`; nếu wallpaper dùng canvas opaque thì không trong suốt được. |
| Build lỗi signing | Project cấu hình `CODE_SIGNING_ALLOWED=NO` để build thử nhanh. Nếu lỗi signing trong Xcode, vào Target → Signing & Capabilities → uncheck "Automatically manage signing". |

---

## 5. GIỚI HẠN & GHI CHÚ

- **`scene` wallpapers**: không hỗ trợ (cần runtime 3D riêng của Wallpaper Engine, chỉ có trên Windows).
- **`webm` (VP9/AV1)**: AVPlayer hỗ trợ tuỳ phiên bản macOS; mp4/H.264 luôn chạy được.
- **Audio visualization** cho web wallpaper (`wallpaperRegisterAudioListener`): bridge đã đăng ký
  nhưng chưa cung cấp dữ liệu audio (cần phân tích audio real-time — future work).
- **Web wallpaper pause**: WKWebView không có API freeze public; khi pause, view được ẩn
  (tiết kiệm GPU compositing), JS timers vẫn chạy nền.
- **LSUIElement = YES**: app chạy dạng agent (không hiện Dock icon), chỉ có Menu Bar item.
- **Code signing**: build không sandbox để CGEventTap hoạt động; để distribute, bật signing +
  hardened runtime (sandbox sẽ chặn event tap).
- **AVPlayer volume/mute**: yêu cầu macOS 10.15+ (deployment target 12.0 thoả mãn);
  hardware decode qua VideoToolbox tự động trên Apple Silicon M1-M4 và Intel.

## 6. ROADMAP (future work)

- [ ] Per-screen wallpaper selection (hiện tại: apply cùng wallpaper cho mọi màn hình)
- [ ] Randomize wallpaper theo timer (như Plasma6 `randomizeTimer`)
- [ ] Audio visualization bridge cho web wallpaper (AudioToolbox / AVAudioEngine tap)
- [ ] Favorites & playlists (đọc `config.json` của WE)
- [ ] Volume fade animation khi play/pause
- [ ] Import file `.wpe` / thư mục offline không qua Steam
