module;
#include <CoreVideo/CVPixelBuffer.h>
#include <CoreVideo/CVPixelBufferIOSurface.h>

export module wavsen.ffi.corevideo;

export namespace wavsen::ffi::corevideo
{
using ::CVPixelBufferGetHeight;
using ::CVPixelBufferGetIOSurface;
using ::CVPixelBufferGetPixelFormatType;
using ::CVPixelBufferGetPlaneCount;
using ::CVPixelBufferGetWidth;
using ::CVPixelBufferIsPlanar;
using ::CVPixelBufferRef;
} // namespace wavsen::ffi::corevideo
