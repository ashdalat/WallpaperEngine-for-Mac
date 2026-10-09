#include "native.hpp"
#include "../../ffi/corevideo/metal.hpp"
#include <Metal/Metal.h>
#include <Foundation/NSString.h>
#include "nv12_to_bgra.metal.h"

#include <memory>
#include <new>

void SetError(const char*& output, const char* message) { output = message; }

struct WavsenMetalAdapter {
    id<MTLDevice>               device { nil };
    id<MTLCommandQueue>         command_queue { nil };
    id<MTLComputePipelineState> nv12_pipeline { nil };
    CVMetalTextureCacheRef      texture_cache {};
    ~WavsenMetalAdapter() {
        [nv12_pipeline release];
        if (texture_cache) CFRelease(texture_cache);
        [command_queue release];
        [device release];
    }
};

struct WavsenMetalTexture {
    id<MTLTexture>    texture { nil };
    CVMetalTextureRef mapped {};
    CVPixelBufferRef  buffer {};
    ~WavsenMetalTexture() {
        [texture release];
        if (mapped) CFRelease(mapped);
        if (buffer) CFRelease(buffer);
    }
};

bool prepare_nv12(WavsenMetalAdapter* state, const char*& error) {
    if (state->nv12_pipeline) return true;
    auto      device        = state->device;
    NSError*  library_error = nil;
    NSString* source =
        [NSString stringWithUTF8String:reinterpret_cast<const char*>(wavsen_nv12_metal)];
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&library_error];
    if (library == nil) {
        SetError(error, "failed to compile Metal NV12 conversion shader");
        return false;
    }
    id<MTLFunction> function = [library newFunctionWithName:@"nv12_to_bgra"];
    [library release];
    if (function == nil) {
        SetError(error, "failed to load Metal NV12 conversion function");
        return false;
    }
    NSError* pipeline_error = nil;
    state->nv12_pipeline    = [device newComputePipelineStateWithFunction:function
                                                                    error:&pipeline_error];
    [function release];
    if (state->nv12_pipeline == nil) {
        SetError(error, "failed to create Metal NV12 conversion pipeline");
    }
    return state->nv12_pipeline != nil;
}

id<MTLTexture> CreateBgraTextureFromNv12(id<MTLDevice> device, WavsenMetalAdapter* state,
                                         CVPixelBufferRef pixel_buffer, OSType pixel_format,
                                         uint32_t width, uint32_t height, void* reusable_texture,
                                         const char*& error) {
    if (state->command_queue == nil || state->nv12_pipeline == nil) {
        SetError(error, "Metal NV12 conversion state is unavailable");
        return nil;
    }

    CVMetalTextureRef y_ref    = nullptr;
    CVMetalTextureRef uv_ref   = nullptr;
    const auto        y_result = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                                           state->texture_cache,
                                                                           pixel_buffer,
                                                                           nullptr,
                                                                           MTLPixelFormatR8Unorm,
                                                                           width,
                                                                           height,
                                                                           0,
                                                                           &y_ref);
    const auto        uv_result =
        CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                  state->texture_cache,
                                                  pixel_buffer,
                                                  nullptr,
                                                  MTLPixelFormatRG8Unorm,
                                                  CVPixelBufferGetWidthOfPlane(pixel_buffer, 1),
                                                  CVPixelBufferGetHeightOfPlane(pixel_buffer, 1),
                                                  1,
                                                  &uv_ref);
    if (y_result != kCVReturnSuccess || uv_result != kCVReturnSuccess || y_ref == nullptr ||
        uv_ref == nullptr) {
        if (y_ref != nullptr) CFRelease(y_ref);
        if (uv_ref != nullptr) CFRelease(uv_ref);
        SetError(error, "failed to create Metal textures for NV12 planes");
        return nil;
    }

    id<MTLTexture> y_texture  = CVMetalTextureGetTexture(y_ref);
    id<MTLTexture> uv_texture = CVMetalTextureGetTexture(uv_ref);
    if (y_texture == nil || uv_texture == nil) {
        CFRelease(y_ref);
        CFRelease(uv_ref);
        SetError(error, "CVMetalTextureCache returned null NV12 plane texture");
        return nil;
    }

    id<MTLTexture> output =
        reusable_texture != nullptr ? (__bridge id<MTLTexture>)reusable_texture : nil;
    if (output != nil &&
        (output.width != width || output.height != height ||
         output.pixelFormat != MTLPixelFormatBGRA8Unorm || output.device != device ||
         (output.usage & (MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite)) !=
             (MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite))) {
        output = nil;
    }
    const bool owns_output = output == nil;
    if (output == nil) {
        MTLTextureDescriptor* descriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        descriptor.usage       = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        descriptor.storageMode = MTLStorageModeShared;
        output                 = [device newTextureWithDescriptor:descriptor];
    }
    if (output == nil) {
        CFRelease(y_ref);
        CFRelease(uv_ref);
        SetError(error, "failed to allocate Metal BGRA video texture");
        return nil;
    }

    id<MTLCommandBuffer>         command_buffer = [state->command_queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder        = [command_buffer computeCommandEncoder];
    if (command_buffer == nil || encoder == nil) {
        if (owns_output) [output release];
        CFRelease(y_ref);
        CFRelease(uv_ref);
        SetError(error, "failed to allocate Metal NV12 command encoder");
        return nil;
    }

    struct Params {
        float y_offset;
        float y_scale;
        float r_cr;
        float g_cb;
        float g_cr;
        float b_cb;
    } params {
        .y_offset =
            pixel_format == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ? 16.0f / 255.0f : 0.0f,
        .y_scale = pixel_format == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ? 255.0f / 219.0f
                                                                                   : 1.0f,
        .r_cr    = 1.402f,
        .g_cb    = -0.344136f,
        .g_cr    = -0.714136f,
        .b_cb    = 1.772f,
    };

    CFTypeRef matrix_attachment =
        CVBufferCopyAttachment(pixel_buffer, kCVImageBufferYCbCrMatrixKey, nullptr);
    if (matrix_attachment != nullptr &&
        CFEqual(matrix_attachment, kCVImageBufferYCbCrMatrix_ITU_R_709_2)) {
        params.r_cr = 1.5748f;
        params.g_cb = -0.187324f;
        params.g_cr = -0.468124f;
        params.b_cb = 1.8556f;
    } else if (matrix_attachment != nullptr &&
               CFEqual(matrix_attachment, kCVImageBufferYCbCrMatrix_ITU_R_2020)) {
        params.r_cr = 1.4746f;
        params.g_cb = -0.164553f;
        params.g_cr = -0.571353f;
        params.b_cb = 1.8814f;
    }
    if (matrix_attachment != nullptr) CFRelease(matrix_attachment);

    [encoder setComputePipelineState:state->nv12_pipeline];
    [encoder setTexture:y_texture atIndex:0];
    [encoder setTexture:uv_texture atIndex:1];
    [encoder setTexture:output atIndex:2];
    [encoder setBytes:&params length:sizeof(params) atIndex:0];
    const auto thread_width =
        (state->nv12_pipeline.threadExecutionWidth < 16 ? state->nv12_pipeline.threadExecutionWidth
                                                        : 16);
    const auto thread_height = (state->nv12_pipeline.maxTotalThreadsPerThreadgroup / thread_width);
    [encoder dispatchThreads:MTLSizeMake(width, height, 1)
        threadsPerThreadgroup:MTLSizeMake(
                                  thread_width, (thread_height < 16 ? thread_height : 16), 1)];
    [encoder endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];

    CFRelease(y_ref);
    CFRelease(uv_ref);
    if (command_buffer.status == MTLCommandBufferStatusError) {
        if (owns_output) [output release];
        SetError(error, "Metal NV12 conversion command failed");
        return nil;
    }
    return output;
}

const char* wavsen_metal_create(void* device, WavsenMetalAdapter** out) {
    if (! out) return "metal adapter output is null";
    *out = nullptr;
    if (! device) return "metal device is null";
    @autoreleasepool {
        auto state = std::unique_ptr<WavsenMetalAdapter>(new (std::nothrow) WavsenMetalAdapter);
        if (! state) return "cannot allocate metal adapter";
        state->device        = [(id<MTLDevice>)device retain];
        state->command_queue = [state->device newCommandQueue];
        if (! state->command_queue) return "cannot create metal command queue";
        if (CVMetalTextureCacheCreate(
                kCFAllocatorDefault, nullptr, state->device, nullptr, &state->texture_cache) !=
            kCVReturnSuccess)
            return "cannot create corevideo metal texture cache";
        *out = state.release();
        return nullptr;
    }
}

void wavsen_metal_destroy(WavsenMetalAdapter* adapter) { delete adapter; }

void wavsen_metal_texture_destroy(WavsenMetalTexture* texture) { delete texture; }

void* wavsen_metal_texture_handle(const WavsenMetalTexture* texture) {
    return texture ? static_cast<void*>(texture->texture) : nullptr;
}

const char* wavsen_metal_import(WavsenMetalAdapter* adapter, void* native_buffer,
                                WavsenMetalTexture** output) {
    if (! adapter || ! native_buffer || ! output) return "invalid metal frame import";
    @autoreleasepool {
        auto buffer = static_cast<CVPixelBufferRef>(native_buffer);
        auto width  = CVPixelBufferGetWidth(buffer);
        auto height = CVPixelBufferGetHeight(buffer);
        auto format = CVPixelBufferGetPixelFormatType(buffer);
        if (! width || ! height) return "empty pixel buffer";
        auto next = std::unique_ptr<WavsenMetalTexture>(new (std::nothrow) WavsenMetalTexture);
        if (! next) return "cannot allocate metal texture";
        if (format == kCVPixelFormatType_32BGRA) {
            if (CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                          adapter->texture_cache,
                                                          buffer,
                                                          nullptr,
                                                          MTLPixelFormatBGRA8Unorm,
                                                          width,
                                                          height,
                                                          0,
                                                          &next->mapped) != kCVReturnSuccess)
                return "cannot map bgra pixel buffer";
            next->texture = [CVMetalTextureGetTexture(next->mapped) retain];
            if (! next->texture) return "corevideo returned no metal texture";
            CFRetain(buffer);
            next->buffer = buffer;
        } else if (format == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
                   format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
            if (CVPixelBufferGetPlaneCount(buffer) != 2) return "invalid nv12 plane count";
            const char* error {};
            if (! prepare_nv12(adapter, error)) return error;
            auto reusable = *output && ! (*output)->mapped ? (*output)->texture : nil;
            auto texture  = CreateBgraTextureFromNv12(adapter->device,
                                                      adapter,
                                                      buffer,
                                                      format,
                                                      width,
                                                      height,
                                                      static_cast<void*>(reusable),
                                                      error);
            if (! texture) return error;
            if (texture == reusable) return nullptr;
            next->texture = texture;
        } else {
            return "unsupported apple pixel format";
        }
        wavsen_metal_texture_destroy(*output);
        *output = next.release();
        CVMetalTextureCacheFlush(adapter->texture_cache, 0);
        return nullptr;
    }
}
