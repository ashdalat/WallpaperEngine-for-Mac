# Kế hoạch chuyển sang Mac Mini M1 — Checklist

> **Trạng thái:** code Swift app đã xong (video + web). Phần scene 3D (MoltenVK) **đã viết xong trong repo** — `scene_backend/` có 2 script + Obj-C++ bridge (`WallpaperEngineMac/Scene/SceneRendererBridge.h/.mm`) + `DesktopSceneView.swift`. Chỉ cần copy 1 lần sang Mac rồi build ở đó.

---

## 1. Copy từ Linux sang Mac (3 thứ)

### a) Source code app (nhỏ, ~1 MB)

```
/home/pc/Desktop/test/WallpaperEngineMac/
```

→ Paste vào `~/Desktop/WallpaperEngineMac/` trên Mac.

Trong đó có:
- `WallpaperEngineMac/` — toàn bộ source Swift app + bridge Obj-C++ (`Scene/SceneRendererBridge.h/.mm`) + `Desktop/DesktopSceneView.swift`
- `scene_backend/` — 2 script `ApplyMoltenVKPatches.sh` + `BuildSceneBackend.sh` (tự clone + patch + build engine)
- `project.yml`, `build.sh`, `README.md`, `CHUYEN_MAC.md`

### b) Wallpaper Workshop content (757 MB, 26 wallpaper)

```
/home/pc/.local/share/Steam/steamapps/workshop/content/431960/
```

→ Paste vào trên Mac:

```
~/Library/Application Support/Steam/steamapps/workshop/content/431960/
```

(Mỗi thư mục con là 1 wallpaper, chứa `project.json` + `scene.pkg` / video / web files. App tự đọc trực tiếp, không cần giải nén.)

### c) Wallpaper Engine shared assets (85 MB) — cần cho scene 3D

```
/home/pc/.local/share/Steam/steamapps/common/wallpaper_engine/assets
```

→ Paste vào trên Mac:

```
~/Library/Application Support/Steam/steamapps/common/wallpaper_engine/assets
```

(Chứa models/textures dùng chung mà nhiều scene wallpaper reference từ `/assets/...`. Wallpaper nào self-contained trong `scene.pkg` thì không cần, nhưng copy đủ cho chắc.)

**Cách copy nhanh:** dùng USB, hoặc `scp` từ Mac:

```bash
# chạy TRÊN MAC, trỏ vào máy Linux
scp -r user@<ip-linux>:/home/pc/Desktop/test/WallpaperEngineMac ~/Desktop/
scp -r user@<ip-linux>:/home/pc/.local/share/Steam/steamapps/workshop/content/431960 \
  "$HOME/Library/Application Support/Steam/steamapps/workshop/content/"
scp -r user@<ip-linux>:/home/pc/.local/share/Steam/steamapps/common/wallpaper_engine/assets \
  "$HOME/Library/Application Support/Steam/steamapps/common/wallpaper_engine/"
```

---

## 2. Cài tool trên Mac (chạy 1 lần)

```bash
# 1. Xcode từ App Store, rồi:
xcode-select --install

# 2. Homebrew (nếu chưa có)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 3. Dependency build scene engine
# (Eigen, nlohmann/json, glslang, SPIRV-Reflect, VMA, miniaudio đã có sẵn trong engine, KHÔNG cần cài)
brew install cmake pkg-config lz4 molten-vk xcodegen git
```

> `molten-vk` = bản dịch Vulkan→Metal của Khronos, chạy tốt trên M1 (arm64).

---

## 3. Build trên Mac (thứ tự)

```bash
cd ~/Desktop/WallpaperEngineMac

# Build tất cả trong 1 lệnh:
./build.sh
# (tự chạy: clone engine → ApplyMoltenVKPatches.sh → BuildSceneBackend.sh
#  → xcodegen → xcodebuild)
```

Hoặc từng bước nếu muốn xem:

```bash
cd ~/Desktop/WallpaperEngineMac/scene_backend

# 1. Lấy source engine render scene (C++20, Vulkan)
git clone --depth 1 https://github.com/catsout/wallpaper-scene-renderer src
# (repo chuyển hướng tới waywallen/open-wallpaper-engine, clone vẫn chạy)

# 2. Patch tương thích MoltenVK (portability extensions + bỏ FD external memory)
bash ApplyMoltenVKPatches.sh

# 3. Build + merge thành 1 static lib (native arch arm64 trên M1)
bash BuildSceneBackend.sh
# → output: scene_backend/build/install/lib/libwescene-renderer.a
# (glslang biên dịch từ source, mất vài phút)

# 4. Build app Swift
cd ~/Desktop/WallpaperEngineMac
./build.sh        # = xcodegen generate + xcodebuild
# hoặc: xcodegen && mở WallpaperEngineMac.xcodeproj trong Xcode, chọn scheme "WallpaperEngineMac", Run
```

---

## 4. Chạy

- Mở app → Menu Bar (icon) → **Gallery** → chọn wallpaper → **Set as Wallpaper**
- Scene 3D cần **Accessibility permission** (System Settings → Privacy & Security → Accessibility) để forward chuột.
- Multi-monitor: mỗi màn hình có 1 wallpaper window riêng.
- Pause khi fullscreen/maximize, khi on battery (cài trong Settings).

---

## 5. Nếu build scene backend lỗi

| Lỗi | Xử lý |
|---|---|
| `vkCreateInstance` fail / `VK_KHR_portability_enumeration` | Chưa chạy `ApplyMoltenVKPatches.sh` — chạy lại |
| `required vulkan device extension ... not supported` (VD `VK_KHR_portability_subset`) | Patch chưa apply, hoặc MoltenVK quá cũ: `brew upgrade molten-vk` |
| `vulkan/vulkan.h file not found` | `brew install molten-vk` (đã bao gồm headers) |
| `libMoltenVK.dylib not found` khi build app | Đã có trong project.yml (`/opt/homebrew/lib`); nếu cài brew ở prefix khác, thêm path vào `LIBRARY_SEARCH_PATHS` + `LD_RUNPATH_SEARCH_PATHS` |
| `lz4` / `liblz4.pc` not found | `brew install lz4 pkg-config` (BuildSceneBackend.sh tự cài) |
| CMake không thấy MoltenVK | Script đã set `VULKAN_SDK` + `CMAKE_PREFIX_PATH` tự động; nếu lỗi thì `export VULKAN_SDK=$(brew --prefix molten-vk)` |
| `ld: library not found for -lwescene-renderer` | Chưa build scene backend: chạy `bash scene_backend/BuildSceneBackend.sh` |

---

## 6. Giới hạn đã biết

- Scene wallpaper dùng **glslang biên dịch GLSL→SPIR-V tại runtime** — chạy được trên MoltenVK, nhưng shader phức tạp có thể cần chỉnh (nếu wallpaper nào lỗi, báo tao tên wallpaper ID).
- External memory/semaphore kiểu FD (dùng cho IPC với Plasma trên Linux) **không cần** trên macOS — đã patch bỏ (chỉ offscreen path dùng, onscreen không đụng tới).
- Video/web wallpaper: dùng AVPlayer / WKWebView gốc của macOS, không cần engine này.
- Resize preview window: scene swapchain tạo fixed-size lúc load, nên resize window preview sẽ stretch chứ không rebuild swapchain (wallpaper window trên desktop thì không affected vì tạo đúng size màn hình).
