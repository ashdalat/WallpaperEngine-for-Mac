export module vrento.image;
import rstd;
export import vrento.texture_types;

using namespace rstd::prelude;
using rstd::FnOnce;

export namespace vrento
{

enum class ImageKind
{
    Pixels,
    Video
};

class ImageDataPtr {
public:
    ImageDataPtr() = default;

    template<typename Release>
    ImageDataPtr(rstd::uint8_t* data, Release release)
        : m_data(data),
          m_release(Some(Box<dyn<FnOnce<void(rstd::uint8_t*)>>>::make(rstd::move(release)))) {}

    ImageDataPtr(const ImageDataPtr&)            = delete;
    ImageDataPtr& operator=(const ImageDataPtr&) = delete;
    ImageDataPtr(ImageDataPtr&& other) noexcept
        : m_data(rstd::exchange(other.m_data, nullptr)), m_release(other.m_release.take()) {}
    ImageDataPtr& operator=(ImageDataPtr&& other) noexcept {
        if (this != &other) {
            reset();
            m_data    = rstd::exchange(other.m_data, nullptr);
            m_release = other.m_release.take();
        }
        return *this;
    }
    ~ImageDataPtr() { reset(); }

    auto     get() const noexcept -> rstd::uint8_t* { return m_data; }
    explicit operator bool() const noexcept { return m_data != nullptr; }
    void     reset() {
        auto* data    = rstd::exchange(m_data, nullptr);
        auto  release = m_release.take();
        if (data) (*release)->call_once(data);
    }

private:
    rstd::uint8_t*                                 m_data {};
    Option<Box<dyn<FnOnce<void(rstd::uint8_t*)>>>> m_release;
};

struct ImageData {
    rstd::int32_t               width {};
    rstd::int32_t               height {};
    rstd::isize                 size {};
    ImageDataPtr                data {};
    Option<rstd::io::ReadRange> video_source;
};

struct ImageHeader {
    ImageKind     kind { ImageKind::Pixels };
    TextureFormat format { TextureFormat::RGBA8 };
    TextureSample sample;
};

struct Image {
    Image()                        = default;
    Image(const Image&)            = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&&)                 = delete;
    Image& operator=(Image&&)      = delete;

    struct Slot {
        rstd::int32_t  width {};
        rstd::int32_t  height {};
        Vec<ImageData> mipmaps;

        explicit operator bool() const { return width > 0 && height > 0 && ! mipmaps.is_empty(); }
    };

    ImageHeader header;
    Vec<Slot>   slots;
    String      key;
};

} // namespace vrento
