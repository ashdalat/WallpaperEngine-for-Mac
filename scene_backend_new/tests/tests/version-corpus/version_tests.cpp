// Per-version corpus tests.
//
// Each category walks the version stamps harvested by the corpus singleton,
// slices the matching entries, and asserts:
//   * the slice is non-empty (otherwise the parameter wouldn't have
//     been generated)
//   * every matching asset satisfies category-specific structural
//     invariants (sane dimensions, plausible bone counts, etc.)
//
// Adding a workshop with a previously-unseen version automatically extends
// the checked data without requiring an allow-list update.

#include <rstd/test/gtest.hpp>

import rstd.cppstd;
import wescene.json;
import wescene.testing.corpus;

using namespace rstd::literals;
using owe::testing::Corpus;

namespace
{

// ----- parameter generators (see header note about static-local refs) ------

const std::vector<std::string>& AllPkgVersions() {
    static const auto v = [] {
        const auto& s = Corpus::instance().pkg_versions();
        return std::vector<std::string>(s.begin(), s.end());
    }();
    return v;
}
const std::vector<int>& AllTexvVersions() {
    static const auto v = [] {
        const auto& s = Corpus::instance().texv_versions();
        return std::vector<int>(s.begin(), s.end());
    }();
    return v;
}
const std::vector<int>& AllTexiVersions() {
    static const auto v = [] {
        const auto& s = Corpus::instance().texi_versions();
        return std::vector<int>(s.begin(), s.end());
    }();
    return v;
}
const std::vector<int>& AllTexbVersions() {
    static const auto v = [] {
        const auto& s = Corpus::instance().texb_versions();
        return std::vector<int>(s.begin(), s.end());
    }();
    return v;
}
const std::vector<int>& AllTexFormats() {
    static const auto v = [] {
        const auto& s = Corpus::instance().tex_formats();
        return std::vector<int>(s.begin(), s.end());
    }();
    return v;
}
const std::vector<int>& AllMdlvVersions() {
    static const auto v = [] {
        const auto& s = Corpus::instance().mdlv_versions();
        return std::vector<int>(s.begin(), s.end());
    }();
    return v;
}
const std::vector<int>& AllMdlsVersions() {
    static const auto v = [] {
        const auto& s = Corpus::instance().mdls_versions();
        return std::vector<int>(s.begin(), s.end());
    }();
    return v;
}
const std::vector<int>& AllMdlaVersions() {
    static const auto v = [] {
        const auto& s = Corpus::instance().mdla_versions();
        return std::vector<int>(s.begin(), s.end());
    }();
    return v;
}

auto JsonI64Or(const owe::Json& value, rstd::int64_t default_value) -> rstd::int64_t {
    return value.as_i64().unwrap_or(rstd::i64(default_value)).to_primitive();
}

} // namespace

// ============================================================================
// scene.pkg version
// ============================================================================

void CheckScenePkgVersion(const std::string& version) {
    auto slice = Corpus::instance().workshops_with_pkg(version);
    ASSERT_FALSE(slice.empty()) << "no workshops for " << version;

    for (const auto& ref : slice) {
        const auto& w = *ref.workshop;
        SCOPED_TRACE("workshop " + w.id);

        auto pkg = w.snapshot.get("pkg"_str);
        ASSERT_TRUE(pkg.is_some());
        auto pkg_version = (*pkg)->get("version"_str);
        ASSERT_TRUE(pkg_version.is_some());
        EXPECT_EQ(rstd::cppstd::to_string(*(*pkg_version)->as_str()), version);
        auto file_count = (*pkg)->get("file_count"_str);
        ASSERT_TRUE(file_count.is_some());
        EXPECT_GT(JsonI64Or(**file_count, 0), 0);
        auto has_scene_json = (*pkg)->get("has_scene_json"_str);
        ASSERT_TRUE(has_scene_json.is_some());
        EXPECT_TRUE((*has_scene_json)->as_bool().unwrap_or(false));

        auto scene = w.snapshot.get("scene"_str);
        ASSERT_TRUE(scene.is_some());
        auto parsed = (*scene)->get("parsed"_str);
        ASSERT_TRUE(parsed.is_some());
        auto error = (*scene)->get("error"_str);
        EXPECT_TRUE((*parsed)->as_bool().unwrap_or(false))
            << "scene.json failed: "
            << (error.is_some() && (*error)->as_str().is_some()
                    ? rstd::cppstd::to_string(*(*error)->as_str())
                    : "");
        auto is_ortho = (*scene)->get("is_ortho"_str);
        if (is_ortho.is_some() && (*is_ortho)->as_bool().unwrap_or(false)) {
            auto ortho = (*scene)->get("ortho"_str);
            ASSERT_TRUE(ortho.is_some());
            auto width  = (*ortho)->get("width"_str);
            auto height = (*ortho)->get("height"_str);
            ASSERT_TRUE(width.is_some());
            ASSERT_TRUE(height.is_some());
            EXPECT_GT(JsonI64Or(**width, 0), 0);
            EXPECT_GT(JsonI64Or(**height, 0), 0);
        }
    }
}

TEST(ScenePkgVersionTest, AllWorkshopsParseAndExposeSaneScene) {
    for (const auto& version : AllPkgVersions()) CheckScenePkgVersion(version);
}

// ============================================================================
// .tex header version stamps
// ============================================================================

static void CheckTexInvariants(const Corpus::TexRef& ref) {
    const auto& w    = *ref.workshop;
    const auto& t    = *ref.tex;
    auto        path = t.get("path"_str);
    SCOPED_TRACE("workshop " + w.id + " tex " +
                 (path.is_some() && (*path)->as_str().is_some()
                      ? rstd::cppstd::to_string(*(*path)->as_str())
                      : ""));
    auto ok         = t.get("ok"_str);
    auto width      = t.get("width"_str);
    auto height     = t.get("height"_str);
    auto map_width  = t.get("map_width"_str);
    auto map_height = t.get("map_height"_str);
    auto count      = t.get("count"_str);
    ASSERT_TRUE(ok.is_some() && width.is_some() && height.is_some() && map_width.is_some() &&
                map_height.is_some() && count.is_some());
    EXPECT_TRUE((*ok)->as_bool().unwrap_or(false));
    EXPECT_GT(JsonI64Or(**width, 0), 0);
    EXPECT_GT(JsonI64Or(**height, 0), 0);
    EXPECT_GT(JsonI64Or(**map_width, 0), 0);
    EXPECT_GT(JsonI64Or(**map_height, 0), 0);
    EXPECT_GT(JsonI64Or(**count, 0), 0);
}

void CheckTexvVersion(int version) {
    auto slice = Corpus::instance().textures_with_texv(version);
    ASSERT_FALSE(slice.empty());
    for (const auto& r : slice) {
        auto value = r.tex->get("texv"_str);
        ASSERT_TRUE(value.is_some());
        EXPECT_EQ(JsonI64Or(**value, -1), version);
        CheckTexInvariants(r);
    }
}
void CheckTexiVersion(int version) {
    auto slice = Corpus::instance().textures_with_texi(version);
    ASSERT_FALSE(slice.empty());
    for (const auto& r : slice) {
        auto value = r.tex->get("texi"_str);
        ASSERT_TRUE(value.is_some());
        EXPECT_EQ(JsonI64Or(**value, -1), version);
        CheckTexInvariants(r);
    }
}
void CheckTexbVersion(int version) {
    auto slice = Corpus::instance().textures_with_texb(version);
    ASSERT_FALSE(slice.empty());
    for (const auto& r : slice) {
        auto value = r.tex->get("texb"_str);
        ASSERT_TRUE(value.is_some());
        EXPECT_EQ(JsonI64Or(**value, -1), version);
        CheckTexInvariants(r);
    }
}
void CheckTexFormat(int format) {
    auto slice = Corpus::instance().textures_with_format(format);
    ASSERT_FALSE(slice.empty());
    for (const auto& r : slice) {
        auto value = r.tex->get("format"_str);
        ASSERT_TRUE(value.is_some());
        EXPECT_EQ(JsonI64Or(**value, -1), format);
        CheckTexInvariants(r);
    }
}

TEST(TextureTexvTest, AllInstancesParse) {
    for (int version : AllTexvVersions()) CheckTexvVersion(version);
}

TEST(TextureTexiTest, AllInstancesParse) {
    for (int version : AllTexiVersions()) CheckTexiVersion(version);
}

TEST(TextureTexbTest, AllInstancesParse) {
    for (int version : AllTexbVersions()) CheckTexbVersion(version);
}

TEST(TextureFormatTest, AllInstancesParse) {
    for (int format : AllTexFormats()) CheckTexFormat(format);
}

// ============================================================================
// .mdl header version stamps
// ============================================================================

static void CheckMdlInvariants(const Corpus::MdlRef& ref) {
    const auto& w    = *ref.workshop;
    const auto& m    = *ref.mdl;
    auto        path = m.get("path"_str);
    SCOPED_TRACE("workshop " + w.id + " mdl " +
                 (path.is_some() && (*path)->as_str().is_some()
                      ? rstd::cppstd::to_string(*(*path)->as_str())
                      : ""));
    // Failed parses are tolerated (some .mdl files are non-puppet 3D
    // models that MdlParser intentionally rejects), but the version
    // stamps must still be readable.
    auto mdlv = m.get("mdlv"_str);
    auto mdls = m.get("mdls"_str);
    auto mdla = m.get("mdla"_str);
    auto ok   = m.get("ok"_str);
    ASSERT_TRUE(mdlv.is_some() && mdls.is_some() && mdla.is_some() && ok.is_some());
    EXPECT_GE(JsonI64Or(**mdlv, -1), 0);
    EXPECT_GE(JsonI64Or(**mdls, -1), 0);
    EXPECT_GE(JsonI64Or(**mdla, -1), 0);
    if ((*ok)->as_bool().unwrap_or(false)) {
        auto bones = m.get("bones"_str);
        ASSERT_TRUE(bones.is_some());
        EXPECT_GT(JsonI64Or(**bones, 0), 0);
    }
}

void CheckMdlvVersion(int version) {
    auto slice = Corpus::instance().mdls_with_mdlv(version);
    ASSERT_FALSE(slice.empty());
    for (const auto& r : slice) {
        auto value = r.mdl->get("mdlv"_str);
        ASSERT_TRUE(value.is_some());
        EXPECT_EQ(JsonI64Or(**value, -1), version);
        CheckMdlInvariants(r);
    }
}
void CheckMdlsVersion(int version) {
    auto slice = Corpus::instance().mdls_with_mdls(version);
    ASSERT_FALSE(slice.empty());
    for (const auto& r : slice) {
        auto value = r.mdl->get("mdls"_str);
        ASSERT_TRUE(value.is_some());
        EXPECT_EQ(JsonI64Or(**value, -1), version);
        CheckMdlInvariants(r);
    }
}
void CheckMdlaVersion(int version) {
    auto slice = Corpus::instance().mdls_with_mdla(version);
    ASSERT_FALSE(slice.empty());
    for (const auto& r : slice) {
        auto value = r.mdl->get("mdla"_str);
        ASSERT_TRUE(value.is_some());
        EXPECT_EQ(JsonI64Or(**value, -1), version);
        CheckMdlInvariants(r);
    }
}

TEST(MdlMdlvTest, AllInstancesExposeStamps) {
    for (int version : AllMdlvVersions()) CheckMdlvVersion(version);
}

TEST(MdlMdlsTest, AllInstancesExposeStamps) {
    for (int version : AllMdlsVersions()) CheckMdlsVersion(version);
}

TEST(MdlMdlaTest, AllInstancesExposeStamps) {
    for (int version : AllMdlaVersions()) CheckMdlaVersion(version);
}

// ============================================================================
// Smoke: corpus must contain at least one workshop with at least one of
// each major asset class. Catches an empty workshop dir / pathing bug.
// ============================================================================

TEST(CorpusSmoke, NonEmpty) {
    const auto& c = Corpus::instance();
    EXPECT_FALSE(c.entries().empty());
    EXPECT_FALSE(c.pkg_versions().empty());
    EXPECT_FALSE(c.texv_versions().empty());
    EXPECT_FALSE(c.mdlv_versions().empty());
}
