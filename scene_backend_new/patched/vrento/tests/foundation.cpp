#include <rstd/macro.hpp>

import rstd;
import rstd.cppstd;
import vrento.texture_types;
import vrento.graphics_types;
import vrento.shader_compile;
import vrento.vulkan;
import vrento.image;
import vrento.video_playback;
import vrento.trace;
import vrento.resource;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace vrento;
using namespace vrento::vulkan;

struct Counts {
    int  preprocess {};
    int  compile {};
    int  reflect {};
    int  destroyed {};
    bool fail {};
};

struct FakeTextureLoader {
    rstd::sync::Arc<Image> image;

    auto LoadTexture(ref<str> key) const
        -> Result<rstd::sync::Arc<Image>, resource::ResourceError> {
        if (key != "pixels"_str) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = String::make("missing pixels"_str),
            });
        }
        return Ok(image.clone());
    }
};

namespace rstd
{
template<>
struct Impl<resource::TextureLoader, FakeTextureLoader> : ImplBase<FakeTextureLoader> {
    auto LoadTexture(ref<str> key) const -> Result<sync::Arc<Image>, resource::ResourceError> {
        return this->self().LoadTexture(key);
    }
};
} // namespace rstd

struct FakeTextureProvider {
    rstd::sync::Arc<Image> image;
    u64                    revision { 1 };

    auto ResolveTextureContent(const resource::TextureRequest& request) const
        -> Result<resource::ImportedTextureContentIdentity, resource::ResourceError> {
        return Ok(resource::ImportedTextureContentIdentity {
            .key      = request.name.clone(),
            .revision = revision,
        });
    }
    auto OpenTextureLoader() const
        -> Result<rstd::sync::Arc<dyn<resource::TextureLoader>>, resource::ResourceError> {
        return Ok(rstd::sync::Arc<dyn<resource::TextureLoader>>::make(
            FakeTextureLoader { image.clone() }));
    }
    auto ResolveVideoPlayback(const resource::TextureRequest&) const
        -> Option<rstd::sync::Arc<dyn<VideoPlayback>>> {
        return None();
    }
};

namespace rstd
{
template<>
struct Impl<resource::TextureContentProvider, FakeTextureProvider> : ImplBase<FakeTextureProvider> {
    auto ResolveTextureContent(const resource::TextureRequest& request) const
        -> Result<resource::ImportedTextureContentIdentity, resource::ResourceError> {
        return this->self().ResolveTextureContent(request);
    }
    auto OpenTextureLoader() const
        -> Result<sync::Arc<dyn<resource::TextureLoader>>, resource::ResourceError> {
        return this->self().OpenTextureLoader();
    }
    auto ResolveVideoPlayback(const resource::TextureRequest& request) const
        -> Option<sync::Arc<dyn<VideoPlayback>>> {
        return this->self().ResolveVideoPlayback(request);
    }
};
} // namespace rstd

struct PlaybackData {
    VideoPlaybackSnapshot snapshot;
    f64                   current {};
    Option<f64>           duration;
};

struct FakePlayback {
    PlaybackData* data;
    auto          Snapshot() const -> VideoPlaybackSnapshot { return data->snapshot; }
    void          PublishTime(f64 current, Option<f64> duration) const {
        data->current  = current;
        data->duration = duration;
    }
};

struct TraceData {
    u64                  next {};
    rstd::array<bool, 4> ended {};
};

struct FakeObserver {
    TraceData* data;
    auto       Begin(RenderEvent) const -> u64 {
        auto token = data->next;
        data->next += u64(1);
        return token;
    }
    void End(u64 token) const {
        auto index = rstd::as_cast<usize>(token);
        rstd_assert(! data->ended[index]);
        data->ended[index] = true;
    }
};

struct FakeShaderBackend {
    explicit FakeShaderBackend(Counts& value): counts(&value) {}
    FakeShaderBackend(const FakeShaderBackend&) = delete;
    FakeShaderBackend(FakeShaderBackend&& other) noexcept
        : counts(std::exchange(other.counts, nullptr)) {}
    ~FakeShaderBackend() {
        if (counts != nullptr) ++counts->destroyed;
    }

    bool Preprocess(ref<str> source, ShaderType stage, SourceLang lang, String& output) const {
        ++counts->preprocess;
        rstd_assert(stage == ShaderType::FRAGMENT && lang == SourceLang::Hlsl);
        if (counts->fail) return false;
        output = rstd::into(source);
        return true;
    }

    bool CompileAndLinkShaderUnits(slice<ShaderCompUnit> units, const ShaderCompOpt& options,
                                   Vec<Uni_ShaderSpv>& output) const {
        ++counts->compile;
        rstd_assert(units.len() == usize(1) && options.target == VulkanTarget::Vulkan_1_1);
        if (counts->fail) return false;
        auto stage         = Box<ShaderSpv>::make();
        stage->stage       = units[usize()].stage;
        stage->entry_point = units[usize()].entry_point.clone();
        stage->spirv.push(0x07230203u);
        output.push(rstd::move(stage));
        return true;
    }

    bool GenReflect(slice<ShaderCode> codes, Vec<Uni_ShaderSpv>&, ShaderReflected& output) const {
        ++counts->reflect;
        rstd_assert(codes.len() == usize(1) && codes[usize()][usize()] == 0x07230203u);
        if (counts->fail) return false;
        (void)output.input_location_map.insert("position"_Str, ShaderReflected::Input {});
        return true;
    }

    Counts* counts;
};

namespace rstd
{
template<>
struct Impl<VideoPlayback, FakePlayback> : ImplBase<FakePlayback> {
    auto Snapshot() const -> VideoPlaybackSnapshot { return this->self().Snapshot(); }
    void PublishTime(f64 current, Option<f64> duration) const {
        this->self().PublishTime(current, duration);
    }
};

template<>
struct Impl<RenderObserver, FakeObserver> : ImplBase<FakeObserver> {
    auto Begin(RenderEvent event) const -> u64 { return this->self().Begin(event); }
    void End(u64 token) const { this->self().End(token); }
};

template<>
struct Impl<ShaderBackend, FakeShaderBackend> : ImplBase<FakeShaderBackend> {
    bool Preprocess(ref<str> source, ShaderType stage, SourceLang lang, String& output) const {
        return this->self().Preprocess(source, stage, lang, output);
    }
    bool CompileAndLinkShaderUnits(slice<ShaderCompUnit> units, const ShaderCompOpt& options,
                                   Vec<Uni_ShaderSpv>& output) const {
        return this->self().CompileAndLinkShaderUnits(units, options, output);
    }
    bool GenReflect(slice<ShaderCode> codes, Vec<Uni_ShaderSpv>& stages,
                    ShaderReflected& output) const {
        return this->self().GenReflect(codes, stages, output);
    }
};
} // namespace rstd

static_assert(static_cast<int>(TextureFormat::BC1) == 0);
static_assert(static_cast<int>(TextureFormat::D32F) == 7);
static_assert(static_cast<int>(ShaderType::GEOMETRY) == 1);
static_assert(static_cast<int>(ShaderMatrixMajor::Row) == 1);
static_assert(static_cast<int>(ShaderMatrixAbi::Hlsl) == 1);
static_assert(static_cast<int>(CompareOp::Always) == 7);
static_assert(static_cast<int>(BlendMode::AlphaToCoverage) == 3);
static_assert(static_cast<int>(CullMode::Back) == 2);
static_assert(static_cast<int>(MeshPrimitive::TRIANGLE) == 1);
static_assert(static_cast<int>(FillMode::ASPECTCROP) == 2);
static_assert(TextureSample {}.wrapS == TextureWrap::REPEAT);
static_assert(TextureSample {}.minFilter == TextureFilter::NEAREST);
static_assert(! TextureSample {}.compare_enable);
static_assert(TextureSample {}.border_color == TextureBorderColor::OpaqueBlack);

void CheckImageDataLifetime() {
    int  released = 0;
    auto make     = [&] {
        auto token = Box<int>::make(1);
        return ImageDataPtr(new rstd::uint8_t[4],
                            [token = rstd::move(token), &released](rstd::uint8_t* data) {
                                released += *token;
                                delete[] data;
                            });
    };
    {
        auto  first  = make();
        auto  second = make();
        auto* pixels = first.get();
        auto  moved  = rstd::move(first);
        rstd_assert(! first && moved.get() == pixels && released == 0);
        second = rstd::move(moved);
        rstd_assert(! moved && second.get() == pixels && released == 1);
        second.reset();
        second.reset();
        rstd_assert(! second && released == 2);
    }
    rstd_assert(released == 2);
    {
        ImageDataPtr empty(nullptr, [&](rstd::uint8_t*) {
            ++released;
        });
        auto         moved = rstd::move(empty);
        moved.reset();
    }
    rstd_assert(released == 2);
    {
        Vec<ImageData> mipmaps;
        for (usize i {}; i < usize(20); ++i) {
            mipmaps.push(ImageData { .data = make() });
        }
        rstd_assert(released == 2);
        mipmaps.truncate(usize(3));
        rstd_assert(released == 19);
    }
    rstd_assert(released == 22);
}

void CheckPendingImageLifetime() {
    Device             device;
    ImageUploadManager uploads(device);
    rstd_assert(uploads.init());
    ImageSlots slots;
    slots.slots.push(AllocatedImageParameters {});
    auto allocation = rstd::sync::Arc<TextureAllocation>::make(rstd::move(slots));
    auto weak       = allocation.downgrade();
    rstd_assert(uploads.QueueTransparentClear(allocation.clone()).is_some());
    allocation.reset();
    rstd_assert(! weak.expired() && uploads.HasPendingUploads());
    uploads.Trim();
    rstd_assert(! weak.expired());
    uploads.DiscardPendingUploads();
    rstd_assert(weak.expired() && ! uploads.HasPendingUploads());
    ImageSlots invalid_slots;
    invalid_slots.slots.push(AllocatedImageParameters {});
    auto  invalid      = rstd::sync::Arc<TextureAllocation>::make(rstd::move(invalid_slots));
    auto  invalid_weak = invalid.downgrade();
    Image image;
    image.slots.push(Image::Slot {});
    rstd_assert(uploads.QueueWrite(invalid.clone(), image).is_none());
    invalid.reset();
    rstd_assert(invalid_weak.expired() && ! uploads.HasPendingUploads());
    uploads.DiscardPendingUploads();
    uploads.destroy();

    RecordedBufferUploads  buffers;
    RecordedImageUploads   images;
    BufferUploadBatchLease buffer_lease;
    ImageUploadBatchLease  image_lease;
    rstd_assert(! buffers.Valid() && ! images.Valid());
    rstd_assert(! buffer_lease.Valid() && buffer_lease.Tickets().is_empty());
    rstd_assert(! image_lease.Valid() && image_lease.Tickets().is_empty());

    BufferAllocation empty;
    auto             moved = rstd::move(empty);
    rstd_assert(! empty && ! moved);
    rstd_assert(moved.buffer() == VK_NULL_HANDLE && moved.offset() == 0 && moved.size() == 0);
}

int main() {
    CheckImageDataLifetime();
    CheckPendingImageLifetime();
    TextureKey key { .width        = i32(32),
                     .height       = i32(16),
                     .usage        = VK_IMAGE_USAGE_SAMPLED_BIT,
                     .format       = TextureFormat::RGBA8,
                     .sample       = {},
                     .mipmap_level = 1,
                     .samples      = VK_SAMPLE_COUNT_1_BIT };
    auto       hash = TextureKey::HashValue(key);
    rstd_assert(hash == TextureKey::HashValue(key));
    auto changed                  = key;
    changed.sample.compare_enable = true;
    rstd_assert(hash != TextureKey::HashValue(changed));
    changed                     = key;
    changed.sample.border_color = TextureBorderColor::TransparentBlack;
    rstd_assert(hash != TextureKey::HashValue(changed));
    rstd_assert(ToVkType(TextureFormat::D32F) == VK_FORMAT_D32_SFLOAT);
    rstd_assert(ToVkType(TextureFormat::BC1) == VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
    rstd_assert(ToVkType(TextureWrap::CLAMP_TO_BORDER) == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    rstd_assert(ToVkType(TextureFilter::NEAREST) == VK_FILTER_NEAREST);
    rstd_assert(ToVkType(CompareOp::GreaterEqual) == VK_COMPARE_OP_GREATER_OR_EQUAL);

    int released = 0;
    {
        auto image = rstd::sync::Arc<Image>::make();
        rstd_assert(image->header.kind == ImageKind::Pixels);
        image->slots.push(Image::Slot {});
        image->slots[usize()].mipmaps.push(ImageData {});
        image->slots[usize()].mipmaps[usize()].data =
            ImageDataPtr(new rstd::uint8_t[4], [&released](rstd::uint8_t* data) {
                delete[] data;
                ++released;
            });
        {
            auto retained = image.clone();
            image         = rstd::sync::Arc<Image>::make();
            rstd_assert(released == 0);
        }
        rstd_assert(released == 1);
    }
    rstd_assert(released == 1);

    {
        Option<rstd::sync::Arc<dyn<resource::TextureLoader>>> loader;
        {
            auto image = rstd::sync::Arc<Image>::make();
            image->slots.push(Image::Slot {});
            image->slots[usize()].mipmaps.push(ImageData {});
            image->slots[usize()].mipmaps[usize()].data =
                ImageDataPtr(new rstd::uint8_t[4], [&released](rstd::uint8_t* data) {
                    delete[] data;
                    ++released;
                });
            FakeTextureProvider provider { rstd::move(image) };
            auto                view = dyn<resource::TextureContentProvider>::from_ref(provider);
            resource::TextureRequest request { .name = String::make("pixels"_str) };
            rstd_assert(view->ResolveTextureContent(request).unwrap().revision == u64(1));
            provider.revision = u64(2);
            rstd_assert(view->ResolveTextureContent(request).unwrap().revision == u64(2));
            loader = Some(view->OpenTextureLoader().unwrap());
        }
        rstd_assert(released == 1);
        rstd_assert((*loader)->LoadTexture("missing"_str).is_err());
        auto first  = (*loader)->LoadTexture("pixels"_str).unwrap();
        auto second = (*loader)->LoadTexture("pixels"_str).unwrap();
        rstd_assert(rstd::addressof(*first) == rstd::addressof(*second));
        loader = None();
        rstd_assert(released == 1);
    }
    rstd_assert(released == 2);

    PlaybackData playback_data;
    playback_data.snapshot.playing       = false;
    playback_data.snapshot.seek_sequence = u64(7);
    auto playback = rstd::sync::Arc<dyn<VideoPlayback>>::make(FakePlayback { &playback_data });
    rstd_assert(! playback->Snapshot().playing);
    rstd_assert(playback->Snapshot().seek_sequence == u64(7));
    playback->PublishTime(f64(2.5), Some(f64(10)));
    rstd_assert(playback_data.current == f64(2.5) && playback_data.duration == Some(f64(10)));

    TraceData trace_data;
    {
        RenderTrace trace(rstd::sync::Arc<dyn<RenderObserver>>::make(FakeObserver { &trace_data }));
        auto        first  = RenderScope(trace, RenderEvent::TexturePlan);
        auto        moved  = rstd::move(first);
        auto        second = RenderScope(trace, RenderEvent::TextureDecode);
        second             = rstd::move(moved);
        rstd_assert(trace_data.ended[usize(1)] && ! trace_data.ended[usize(0)]);
        trace = {};
    }
    rstd_assert(trace_data.next == u64(2) && trace_data.ended[usize(0)]);
    {
        auto disabled = RenderScope({}, RenderEvent::Instance);
    }

    Counts counts;
    {
        auto   backend = Box<dyn<ShaderBackend>>::make(FakeShaderBackend(counts));
        auto   view    = backend.as_ref();
        String preprocessed;
        rstd_assert(
            view->Preprocess("source"_str, ShaderType::FRAGMENT, SourceLang::Hlsl, preprocessed));
        rstd_assert(preprocessed.as_str() == "source"_str);
        rstd::array<ShaderCompUnit, 1> units {
            ShaderCompUnit { ShaderType::FRAGMENT, "source"_Str, "entry"_Str, SourceLang::Hlsl },
        };
        Vec<Uni_ShaderSpv> stages;
        rstd_assert(view->CompileAndLinkShaderUnits(units.as_slice(), ShaderCompOpt {}, stages));
        rstd_assert(stages.len() == usize(1) &&
                    stages[usize()]->entry_point.as_str() == "entry"_str);
        Vec<ShaderCode> codes;
        codes.push(stages[usize()]->spirv.clone());
        ShaderReflected reflected;
        rstd_assert(view->GenReflect(codes.as_slice(), stages, reflected));
        rstd_assert(reflected.input_location_map.contains_key("position"_str));
        counts.fail = true;
        rstd_assert(
            ! view->Preprocess("source"_str, ShaderType::FRAGMENT, SourceLang::Hlsl, preprocessed));
        rstd_assert(! view->CompileAndLinkShaderUnits(units.as_slice(), ShaderCompOpt {}, stages));
        rstd_assert(! view->GenReflect(codes.as_slice(), stages, reflected));
        rstd_assert(counts.destroyed == 0);
    }
    rstd_assert(counts.preprocess == 2 && counts.compile == 2 && counts.reflect == 2);
    rstd_assert(counts.destroyed == 1);
}
