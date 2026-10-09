module;
#include <CoreFoundation/CoreFoundation.h>

export module wavsen.ffi.corefoundation;

export namespace wavsen::ffi::corefoundation
{
using ::CFEqual;
using ::CFRelease;
using ::CFRetain;
using ::CFTypeRef;
using ::kCFAllocatorDefault;
} // namespace wavsen::ffi::corefoundation
