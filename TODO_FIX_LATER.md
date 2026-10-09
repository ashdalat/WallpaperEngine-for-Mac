# Ghi chú — lỗi còn lại, sửa sau

> Trạng thái: **dừng điều tra** (user: "cố sửa cái này chắc cũng không ăn thua, cứ để vậy đi").
> Wallpaper Frieren-Pagoda of flowers (3426865175) giờ load được puppet + texture,
> render tương đối ổn. Lỗi dưới đây chỉ ảnh hưởng animation, không ảnh hưởng việc
> load/render.

---

## 1. Animation id lệch 1 byte ở MDLV0023 / MDLA0006 (ALT format)

**File:** `scene_backend/src/WPMdlParser.cpp`, đoạn cuối `Parse()`:

```cpp
if(alt_mdl_format)
{
    f.ReadUint8();
    f.ReadUint8();
}
```

**Hiện tượng:** ở file MDLV0023 (MDLA0006), giữa 2 animation có **35 byte** gap
(2 byte skip + 33 byte 0 + id) — số LẺ. Parser chỉ skip 2 byte nên khi đọc id của
animation thứ 2 trở đi bị lệch 1 byte → id đọc ra = `id_thật << 8`.

- XME_puppet.mdl: scene.json reference animation **1362** ("动画 2"), parser đọc được
  **348672** (= 1362 << 8). Animation layer không match → animation không chơi
  (nhân vật đứng yên ở pose đầu tiên).
- anim[0] vẫn đúng (id 1085) vì không có gap trước nó.

**Bằng chứng:**
- MDLV0019/MDLA0005 (working, ex: 3098540596 Frieren-puppet.mdl): gap = 2 + 32 byte 0
  = 34 (chẵn) → skip 2 byte là đúng, id khớp scene.json (126416, 13994, 224699).
- MDLV0023/MDLA0006 (3426865175 XME_puppet.mdl): gap = 35 (lẻ). Nếu skip 3 byte
  thay vì 2 thì id căn đúng (882494 + 3 + 32 = 882529 = đúng vị trí id 1362).

**Hướng sửa:** thử skip 3 byte cho MDLA0006, hoặc detect: sau khi skip 2 byte, nếu
`(id >> 8)` là id hợp lệ (khớp tên animation trong scene.json / `id % 256 == 0` pattern)
thì lùi lại 1 byte. Cần thêm file MDLV0023 mẫu để xác nhận rule (chưa rõ gap 33 byte
là do MDLA0006 hay do mdlv 23).

**Ảnh hưởng:** chỉ animation. Mesh + texture + bone đều parse đúng
(XME: 42 bones, fll: 23 bones, mỗi puppet 2 anims).

---

## 2. Lỗi pre-existing (có ở MỌI wallpaper, kể cả wallpaper đang hoạt động tốt)

- `_rt_shadowAtlas` — render target shadow atlas chưa implement
  (`WPSceneParser.cpp:244/340`)
- `_alias_lightCookie.tex` — texture alias chưa có trong VFS
- `ui_editor_effect_glitter_title` — shader link error: vertex stage `vec2 v_TexCoord`
  vs fragment stage `vec4 v_TexCoord` (`Shader.cpp:426`)
- `[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Metal does not support disabling
  primitive restart` — benign

---

## Đã sửa trong commit này

- **MDLV0023 MDL** (`WPMdlParser.cpp`): MDLV0023 chèn 1 section lạ (~58-88KB, layout
  chưa biết — có vẻ physics/cloth: start `01 01 00 00`, end `00 00 00 01`, records
  12 byte với u32=68 xen 67/68) giữa index buffer và MDLS bones. Parser giờ scan
  forward tìm MDLS header hợp lệ (`ScanMDLSVersion` + `ValidateBoneSection`) + EOF guards
  trên loop tìm MDLA và loop đọc anim id.
- **TEXB0004 texture** (`WPTexImageParser.cpp`): TEXB0004 (174/399 tex trong workshop
  dùng format này) có thêm 2 field per-image trước mipmap count: `[ImageType][unk=0]`
  (ImageType: 13=PNG, 2=JPEG, -1=UNKNOWN/raw). Parser giờ skip 2 field này và giải
  PNG/JPEG bằng stb_image khi ImageType != UNKNOWN. Layout texb=4:
  `[header][count][per image: type, unk, mipmap_count, (mip: w,h,lz4,decsz,srcsz,data)*]
  [TEXS frame table nếu có]`.
