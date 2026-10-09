module;
#include <CoreAudio/AudioHardware.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

export module wavsen.ffi.coreaudio;

export namespace wavsen::ffi::coreaudio
{
inline constexpr const char* aggregate_name_key      = kAudioAggregateDeviceNameKey;
inline constexpr const char* aggregate_uid_key       = kAudioAggregateDeviceUIDKey;
inline constexpr const char* aggregate_private_key   = kAudioAggregateDeviceIsPrivateKey;
inline constexpr const char* aggregate_taps_key      = kAudioAggregateDeviceTapListKey;
inline constexpr const char* aggregate_autostart_key = kAudioAggregateDeviceTapAutoStartKey;
using ::AudioBufferList;
using ::AudioComponent;
using ::AudioComponentDescription;
using ::AudioComponentFindNext;
using ::AudioComponentInstanceDispose;
using ::AudioComponentInstanceNew;
using ::AudioDeviceCreateIOProcID;
using ::AudioDeviceDestroyIOProcID;
using ::AudioDeviceIOProcID;
using ::AudioDeviceStart;
using ::AudioDeviceStop;
using ::AudioHardwareCreateAggregateDevice;
using ::AudioHardwareDestroyAggregateDevice;
using ::AudioObjectGetPropertyData;
using ::AudioObjectID;
using ::AudioObjectPropertyAddress;
using ::AudioOutputUnitStart;
using ::AudioOutputUnitStop;
using ::AudioStreamBasicDescription;
using ::AudioTimeStamp;
using ::AudioUnit;
using ::AudioUnitInitialize;
using ::AudioUnitRenderActionFlags;
using ::AudioUnitSetProperty;
using ::AudioUnitUninitialize;
using ::AURenderCallbackStruct;
using ::CFArrayCreate;
using ::CFArrayRef;
using ::CFDictionaryCreate;
using ::CFDictionaryRef;
using ::CFNumberCreate;
using ::CFNumberRef;
using ::CFRelease;
using ::CFStringCreateWithCString;
using ::CFStringRef;
using ::CFTypeRef;
using ::CFUUIDCreate;
using ::CFUUIDCreateString;
using ::Float64;
using ::kAudioFormatFlagIsFloat;
using ::kAudioFormatFlagsNativeFloatPacked;
using ::kAudioFormatLinearPCM;
using ::kAudioHardwarePropertyDefaultOutputDevice;
using ::kAudioObjectPropertyElementMain;
using ::kAudioObjectPropertyScopeGlobal;
using ::kAudioObjectSystemObject;
using ::kAudioObjectUnknown;
using ::kAudioTapPropertyFormat;
using ::kAudioUnitManufacturer_Apple;
using ::kAudioUnitProperty_SetRenderCallback;
using ::kAudioUnitProperty_StreamFormat;
using ::kAudioUnitScope_Input;
using ::kAudioUnitSubType_DefaultOutput;
using ::kAudioUnitType_Output;
using ::kCFAllocatorDefault;
using ::kCFBooleanFalse;
using ::kCFBooleanTrue;
using ::kCFNumberSInt32Type;
using ::kCFStringEncodingUTF8;
using ::kCFTypeArrayCallBacks;
using ::kCFTypeDictionaryKeyCallBacks;
using ::kCFTypeDictionaryValueCallBacks;
using ::OSStatus;
using ::SInt32;
using ::UInt32;
} // namespace wavsen::ffi::coreaudio
