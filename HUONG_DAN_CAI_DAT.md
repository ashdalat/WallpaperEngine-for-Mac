# Hướng dẫn cài đặt Wallpaper Engine Mac

Ứng dụng macOS **native** cho phép đặt **hình nền động** từ Steam Wallpaper Engine
làm wallpaper thật của máy (video, scene 3D, web) — hỗ trợ đa màn hình, tạm
dừng thông minh, điều chỉnh âm lượng.

---

## Yêu cầu

- **Mac Apple Silicon** (chip M1 / M2 / M3 / M4 trở lên)
- **macOS 15.0 (Sequoia)** trở lên
- Đã cài **Steam** và có **Wallpaper Engine** (Steam appid `431960`),
  đã tải wallpaper về máy (hoặc có file MP4 trong thư viện Wallpaper Engine)

> Không cần cài thêm gì khác — app đã tự chứa toàn bộ thư viện cần thiết
> (FFmpeg, MoltenVK, ...) bên trong. Không cần Homebrew.

---

## Cài đặt

1. **Copy** `WallpaperEngineMac.app` vào thư mục `/Applications`
   (kéo thả trong Finder, hoặc `Cmd + C` / `Cmd + V`).
2. **Mở lần đầu tiên:** giữ phím **Control** rồi **click** (hoặc right-click)
   vào `WallpaperEngineMac.app` → chọn **Open** → nhấn **Open** trong hộp
   thoại cảnh báo của macOS.

   > App được ký ad-hoc (không có certificate trả phí), nên macOS Gatekeeper
   > sẽ hỏi 1 lần rồi nhớ. Sau đó có thể mở bình thường bằng double-click.

3. Sau khi mở, nhìn lên **thanh menu trên cùng** — có biểu tượng ảnh/phim
   (photo.on.rectangle) của Wallpaper Engine.

---

## Sử dụng

Nhấn vào biểu tượng thanh menu:

| Mục | Chức năng |
|---|---|
| **Open Gallery…** | Mở cửa sổ chọn wallpaper — chọn wallpaper muốn dùng |
| **Random Wallpaper** (`r`) | Đổi ngẫu nhiên 1 wallpaper |
| **Remove Wallpaper** | Gỡ wallpaper, về nền thường |
| **Pause / Resume** (`p`) | Tạm dừng / tiếp tục |
| **Display Mode** | Cách hiển thị: Crop (lấp màn hình) / Fit / Stretch… |
| **Pause Mode** | Tạm dừng khi cửa sổ khác phủ lên, khi fullscreen, … |
| **Volume** | Âm lượng wallpaper có tiếng |
| **Mute Audio** | Tắt tiếng |
| **Mouse Input (Web)** | Cho phép wallpaper web bắt chuột |
| **Settings…** (`,`) | Cài đặt, gồm **đường dẫn thư mục Steam** |
| **Quit Wallpaper Engine** (`q`) | Thoát |

### Nếu không thấy wallpaper trong Gallery

Mở **Settings…** → chọn đúng **thư mục cài Steam** (thư mục chứa `steamapps/`).
App quét các thư mục:

```
<Steam>/steamapps/workshop/content/431960/<workshopId>/   (wallpaper Workshop)
<Steam>/steamapps/common/wallpaper_engine/projects/         (project mặc định & tự tạo)
```

App sẽ **tự động nhớ** wallpaper cuối cùng bạn dùng và đặt lại mỗi lần mở máy.

---

## Gỡ cài đặt

Kéo `WallpaperEngineMac.app` vào **Trash**. (Tuỳ chọn) xoá thêm thư mục
`~/Library/Preferences/com.wallpaperengine.mac.plist` để reset cài đặt.

---

## Ghi chú

- App **không phải** của Valve — chỉ đọc thư viện Wallpaper Engine có sẵn
  trên máy bạn, không tải gì thêm.
- Nếu có lỗi, chạy trong Terminal để xem log:
  `/Applications/WallpaperEngineMac.app/Contents/MacOS/WallpaperEngineMac`
- Mã nguồn mở: Swift / AppKit / SwiftUI + engine `open-wallpaper-engine`
  (MoltenVK/Vulkan) — xem trong thư mục source đính kèm.
