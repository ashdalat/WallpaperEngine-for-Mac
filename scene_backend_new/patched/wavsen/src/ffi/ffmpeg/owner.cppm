export module wavsen.ffi.ffmpeg.owner;

import rstd;
import wavsen.ffi.ffmpeg;

export namespace wavsen::ffi::ffmpeg
{
template<typename T, void (*Release)(T*&)>
class AvOwner {
public:
    AvOwner() = default;
    explicit AvOwner(T* value): value_(value) {}
    AvOwner(const AvOwner&)            = delete;
    AvOwner& operator=(const AvOwner&) = delete;

    AvOwner(AvOwner&& other) noexcept: value_(rstd::exchange(other.value_, nullptr)) {}
    AvOwner& operator=(AvOwner&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = rstd::exchange(other.value_, nullptr);
        }
        return *this;
    }

    ~AvOwner() { reset(); }

    void reset(T* value = nullptr) {
        if (value_) Release(value_);
        value_ = value;
    }

    T*       get() const { return value_; }
    T*       operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }

private:
    T* value_ { nullptr };
};

void release_format(AVFormatContext*& value) {
    if (value) avformat_close_input(&value);
}
void release_codec(AVCodecContext*& value) {
    if (value) avcodec_free_context(&value);
}
void release_frame(AVFrame*& value) {
    if (value) av_frame_free(&value);
}
void release_packet(AVPacket*& value) {
    if (value) av_packet_free(&value);
}
void release_sws(SwsContext*& value) {
    if (value) sws_freeContext(value);
    value = nullptr;
}
void release_buffer(AVBufferRef*& value) {
    if (value) av_buffer_unref(&value);
}

using FmtCtxPtr   = AvOwner<AVFormatContext, release_format>;
using CodecCtxPtr = AvOwner<AVCodecContext, release_codec>;
using FramePtr    = AvOwner<AVFrame, release_frame>;
using PacketPtr   = AvOwner<AVPacket, release_packet>;
using SwsPtr      = AvOwner<SwsContext, release_sws>;
using BufRefPtr   = AvOwner<AVBufferRef, release_buffer>;

} // namespace wavsen::ffi::ffmpeg
