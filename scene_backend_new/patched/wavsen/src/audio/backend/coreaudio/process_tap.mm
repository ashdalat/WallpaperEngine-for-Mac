#include "process_tap.hpp"
#include <CoreAudio/AudioHardwareTapping.h>
#include <CoreAudio/CATapDescription.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Foundation/NSValue.h>
#include <unistd.h>

namespace wavsen::audio::backend
{
auto create_process_tap(unsigned int* tap, const void** uid) noexcept -> int {
    *tap = kAudioObjectUnknown;
    *uid = nullptr;
    if (@available(macOS 14.2, *)) {
        @autoreleasepool {
            AudioObjectPropertyAddress address {
                kAudioHardwarePropertyTranslatePIDToProcessObject,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain,
            };
            const pid_t   pid     = getpid();
            AudioObjectID process = kAudioObjectUnknown;
            UInt32        size    = sizeof(process);
            auto          status  = AudioObjectGetPropertyData(
                kAudioObjectSystemObject, &address, sizeof(pid), &pid, &size, &process);
            NSArray*          excluded = status == 0 && process != kAudioObjectUnknown
                                             ? @[[NSNumber numberWithUnsignedInt:process]]
                                             : @[];
            CATapDescription* description =
                [[CATapDescription alloc] initMonoGlobalTapButExcludeProcesses:excluded];
            if (! description) return -1;
            [description setPrivate:YES];
            [description setMuteBehavior:CATapUnmuted];
            [description setName:@"wavsen system audio tap"];
            NSUUID* uuid = [[NSUUID alloc] init];
            [description setUUID:uuid];
            CFStringRef identifier = CFStringCreateWithCString(
                kCFAllocatorDefault, [[uuid UUIDString] UTF8String], kCFStringEncodingUTF8);
            [uuid release];
            if (! identifier) {
                [description release];
                return -1;
            }
            status = AudioHardwareCreateProcessTap(description, tap);
            [description release];
            if (status != 0) {
                CFRelease(identifier);
                return status;
            }
            *uid = identifier;
            return 0;
        }
    }
    return -2;
}

void destroy_process_tap(unsigned int tap) noexcept {
    if (@available(macOS 14.2, *)) {
        if (tap != kAudioObjectUnknown) AudioHardwareDestroyProcessTap(tap);
    }
}
} // namespace wavsen::audio::backend
