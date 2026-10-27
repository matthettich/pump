// System EQ: pump EQ on everything the Mac plays.
//
// A menu-bar app (macOS 14.2+). It taps the mix of every other app with a Core Audio process tap (muted for as long as
// the tap exists), runs it through the pump EQ Audio Unit bundled inside the app (Contents/PlugIns/pump-eq.component,
// the same build as the plugin, with its own window), and plays the result on the current output device. Turning it
// off destroys the tap, so the Mac goes back to its untouched output.
//
// It follows the Mac's own output and sample rate, keeps separate EQ settings for each output (or one for all), skips
// the EQ during silence and stops the audio device entirely while no app is playing.
//
// Audio path:   every app (except this one) -> tap -> private aggregate device [tap + output device]
//               -> IOProc -> pump EQ (AudioUnitRender) -> output device
//
// Nothing on the audio thread allocates, locks or calls Objective-C.

#import <Cocoa/Cocoa.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <ServiceManagement/ServiceManagement.h>
#include <dlfcn.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

static NSString* const kAppName = @"System EQ";
static const UInt32 kMaxFrames = 4096;

// ---------------------------------------------------------------------------------------------------------------------
// small Core Audio helpers

static AudioObjectPropertyAddress addr(AudioObjectPropertySelector sel,
                                       AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
{
    return { sel, scope, kAudioObjectPropertyElementMain };
}

template <typename T>
static bool getProp(AudioObjectID obj, AudioObjectPropertySelector sel, T& out,
                    AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
{
    AudioObjectPropertyAddress a = addr(sel, scope);
    UInt32 size = sizeof(T);
    return AudioObjectGetPropertyData(obj, &a, 0, nullptr, &size, &out) == noErr;
}

static NSString* stringProp(AudioObjectID obj, AudioObjectPropertySelector sel)
{
    CFStringRef s = nullptr;
    if (!getProp(obj, sel, s) || s == nullptr)
        return nil;
    return (__bridge_transfer NSString*)s;
}

static AudioObjectID defaultOutputDevice()
{
    AudioObjectID dev = kAudioObjectUnknown;
    getProp(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, dev);
    return dev;
}

static AudioObjectID ownProcessObject()
{
    AudioObjectPropertyAddress a = addr(kAudioHardwarePropertyTranslatePIDToProcessObject);
    pid_t pid = getpid();
    AudioObjectID obj = kAudioObjectUnknown;
    UInt32 size = sizeof(obj);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, sizeof(pid), &pid, &size, &obj);
    return obj;
}

static void writeChannel(AudioBufferList* list, UInt32 ch, const float* src, UInt32 frames, UInt32 offset)
{
    UInt32 first = 0;
    for (UInt32 b = 0; b < list->mNumberBuffers; ++b)
    {
        AudioBuffer& buf = list->mBuffers[b];
        const UInt32 n = buf.mNumberChannels;
        if (ch < first + n && buf.mData != nullptr && n > 0)
        {
            const UInt32 avail = buf.mDataByteSize / (sizeof(float) * n);
            if (offset >= avail)
                return;
            const UInt32 count = (offset + frames <= avail) ? frames : avail - offset;
            float* dst = static_cast<float*>(buf.mData) + (ch - first) + offset * n;
            for (UInt32 i = 0; i < count; ++i)
                dst[i * n] = src[i];
            return;
        }
        first += n;
    }
}

static UInt32 framesIn(const AudioBufferList* list)
{
    for (UInt32 b = 0; b < list->mNumberBuffers; ++b)
        if (list->mBuffers[b].mNumberChannels > 0)
            return list->mBuffers[b].mDataByteSize / (sizeof(float) * list->mBuffers[b].mNumberChannels);
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// the audio engine: tap + aggregate device + IOProc, rendering through one AudioUnit

struct Engine
{
    AudioUnit au = nullptr;
    double auRate = 0.0;

    AudioObjectID tap = kAudioObjectUnknown;
    AudioObjectID aggregate = kAudioObjectUnknown;
    AudioObjectID output = kAudioObjectUnknown;
    AudioDeviceIOProcID ioProc = nullptr;
    UInt32 outLeft = 0, outRight = 1;     // 0-based output channels (the device's preferred stereo pair)
    UInt32 tapBuffer = 0;                 // index of the tap's buffer in the aggregate's input list
    double outputRate = 0.0;              // the output device's rate when we started

    // audio-thread buffers (allocated once)
    std::vector<float> inL, inR, outL, outR;
    Float64 sampleTime = 0.0;
    std::atomic<bool> running { false };   // tap + device exist (EQ on)
    bool paused = false;                   // device stopped while nothing plays (main thread)

    // frames of continuous silence at the tap (written by the audio thread, read by the main thread)
    std::atomic<uint64_t> silentFrames { 0 };

    Engine() : inL(kMaxFrames), inR(kMaxFrames), outL(kMaxFrames), outR(kMaxFrames) {}

    // --- the AudioUnit's input: the tap samples of the current slice
    static OSStatus renderInput(void* refCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32,
                                UInt32 frames, AudioBufferList* io)
    {
        Engine* e = static_cast<Engine*>(refCon);
        const float* src[2] = { e->inL.data(), e->inR.data() };
        for (UInt32 b = 0; b < io->mNumberBuffers && b < 2; ++b)
        {
            if (io->mBuffers[b].mData == nullptr)
                io->mBuffers[b].mData = const_cast<float*>(src[b]);
            else if (io->mBuffers[b].mData != src[b])
                std::memcpy(io->mBuffers[b].mData, src[b], frames * sizeof(float));
            io->mBuffers[b].mDataByteSize = frames * sizeof(float);
        }
        return noErr;
    }

    static OSStatus ioProcFn(AudioObjectID, const AudioTimeStamp*, const AudioBufferList* inInput,
                             const AudioTimeStamp*, AudioBufferList* outOutput, const AudioTimeStamp*, void* ctx)
    {
        Engine* e = static_cast<Engine*>(ctx);

        // silence every output channel first
        for (UInt32 b = 0; b < outOutput->mNumberBuffers; ++b)
            if (outOutput->mBuffers[b].mData != nullptr)
                std::memset(outOutput->mBuffers[b].mData, 0, outOutput->mBuffers[b].mDataByteSize);

        const UInt32 total = framesIn(outOutput);
        if (total == 0 || e->au == nullptr)
            return noErr;

        // only the tap's buffer: the output device's own inputs (if it has any) are switched off and never read
        const AudioBuffer* tapBuf = (e->tapBuffer < inInput->mNumberBuffers) ? &inInput->mBuffers[e->tapBuffer]
                                                                              : nullptr;
        const UInt32 tapCh = (tapBuf != nullptr && tapBuf->mData != nullptr) ? tapBuf->mNumberChannels : 0;
        const UInt32 tapFrames = tapCh > 0 ? tapBuf->mDataByteSize / (sizeof(float) * tapCh) : 0;
        const UInt32 left = e->outLeft < tapCh ? e->outLeft : 0;
        const UInt32 right = e->outRight < tapCh ? e->outRight : (tapCh > 1 ? 1 : 0);

        // after 2 s of digital silence (EQ tails long gone) skip the EQ entirely: an app holding its output open
        // without playing costs almost nothing. The first non-silent slice brings it straight back.
        const uint64_t dozeAfter = (uint64_t)(2.0 * (e->auRate > 0.0 ? e->auRate : 48000.0));

        UInt32 done = 0;
        while (done < total)
        {
            const UInt32 n = (total - done) < kMaxFrames ? (total - done) : kMaxFrames;
            const bool have = tapCh > 0 && tapFrames >= done + n;

            if (have)
            {
                const float* src = static_cast<const float*>(tapBuf->mData) + done * tapCh;
                for (UInt32 i = 0; i < n; ++i)
                {
                    e->inL[i] = src[i * tapCh + left];
                    e->inR[i] = src[i * tapCh + right];
                }
                // channels other than the stereo pair (multichannel interfaces) pass through untouched
                for (UInt32 c = 0; c < tapCh; ++c)
                    if (c != left && c != right)
                    {
                        for (UInt32 i = 0; i < n; ++i)
                            e->outL[i] = src[i * tapCh + c];
                        writeChannel(outOutput, c, e->outL.data(), n, done);
                    }
            }
            else
            {
                std::memset(e->inL.data(), 0, n * sizeof(float));
                std::memset(e->inR.data(), 0, n * sizeof(float));
            }

            bool sound = false;
            for (UInt32 i = 0; i < n && !sound; ++i)
                sound = std::fabs(e->inL[i]) > 1.0e-6f || std::fabs(e->inR[i]) > 1.0e-6f;   // above -120 dBFS
            const uint64_t silent = sound ? 0 : e->silentFrames.load(std::memory_order_relaxed) + n;
            e->silentFrames.store(silent, std::memory_order_relaxed);
            if (silent > dozeAfter)
            {
                e->sampleTime += n;
                done += n;   // the pair is already zeroed
                continue;
            }

            struct { AudioBufferList list; AudioBuffer second; } out;
            out.list.mNumberBuffers = 2;
            out.list.mBuffers[0] = { 1, n * (UInt32)sizeof(float), e->outL.data() };
            out.second = { 1, n * (UInt32)sizeof(float), e->outR.data() };

            AudioTimeStamp ts = {};
            ts.mSampleTime = e->sampleTime;
            ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRenderActionFlags flags = 0;

            if (AudioUnitRender(e->au, &flags, &ts, 0, n, &out.list) != noErr)
            {
                // pass the input through rather than dropping out
                std::memcpy(e->outL.data(), e->inL.data(), n * sizeof(float));
                std::memcpy(e->outR.data(), e->inR.data(), n * sizeof(float));
                out.list.mBuffers[0].mData = e->outL.data();
                out.second.mData = e->outR.data();
            }
            e->sampleTime += n;

            writeChannel(outOutput, e->outLeft, static_cast<float*>(out.list.mBuffers[0].mData), n, done);
            writeChannel(outOutput, e->outRight, static_cast<float*>(out.second.mData), n, done);
            done += n;
        }
        return noErr;
    }

    // --- set the AudioUnit up for a sample rate (main thread, engine stopped)
    bool prepareUnit(double rate)
    {
        if (au == nullptr)
            return false;
        if (rate == auRate)
            return true;

        AudioUnitUninitialize(au);

        AudioStreamBasicDescription fmt = {};
        fmt.mSampleRate = rate;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
        fmt.mBytesPerPacket = sizeof(float);
        fmt.mFramesPerPacket = 1;
        fmt.mBytesPerFrame = sizeof(float);
        fmt.mChannelsPerFrame = 2;
        fmt.mBitsPerChannel = 32;

        // the render callback hands over exactly this format (stereo, float, non-interleaved) and slices of at most
        // kMaxFrames: if the unit won't take any of it, it can't run (initializing anyway could pick another layout)
        UInt32 maxFrames = kMaxFrames;
        AURenderCallbackStruct cb = { renderInput, this };
        OSStatus st = AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
        if (st == noErr)
            st = AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, sizeof(fmt));
        if (st == noErr)
            st = AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
                                      &maxFrames, sizeof(maxFrames));
        if (st == noErr)
            st = AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
        if (st != noErr)
        {
            NSLog(@"%@: the EQ refused its stereo format at %.0f Hz (error %d)", kAppName, rate, (int)st);
            auRate = 0.0;
            return false;
        }

        if (AudioUnitInitialize(au) != noErr)
        {
            NSLog(@"%@: the EQ could not be initialized at %.0f Hz", kAppName, rate);
            auRate = 0.0;
            return false;
        }
        auRate = rate;
        return true;
    }

    // --- start: tap everything but this process, build the aggregate, run (main thread)
    NSString* start()
    {
        stop();

        output = defaultOutputDevice();
        if (output == kAudioObjectUnknown)
            return @"No output device.";

        NSString* outputUID = stringProp(output, kAudioDevicePropertyDeviceUID);
        if (outputUID == nil)
            return @"The output device has no identifier.";

        // the output's preferred stereo pair (1-based in Core Audio)
        UInt32 pair[2] = { 1, 2 };
        {
            AudioObjectPropertyAddress a = addr(kAudioDevicePropertyPreferredChannelsForStereo,
                                                kAudioObjectPropertyScopeOutput);
            UInt32 size = sizeof(pair);
            if (AudioObjectGetPropertyData(output, &a, 0, nullptr, &size, pair) != noErr)
                pair[0] = 1, pair[1] = 2;
        }
        outLeft = pair[0] > 0 ? pair[0] - 1 : 0;
        outRight = pair[1] > 0 ? pair[1] - 1 : 1;

        Float64 rate = 48000.0;
        getProp(output, kAudioDevicePropertyNominalSampleRate, rate);
        outputRate = rate;

        // what every process except this one plays to this output (apps playing to other devices, e.g. a DAW on an
        // audio interface, are left alone), muted for as long as the tap exists (also while paused, so nothing ever
        // plays unprocessed; destroying the tap gives the Mac its own output back)
        const AudioObjectID me = ownProcessObject();
        if (me == kAudioObjectUnknown)
            return @"Couldn't identify this app to Core Audio.";   // never tap without excluding ourselves

        CATapDescription* desc = [[CATapDescription alloc] initExcludingProcesses:@[ @(me) ]
                                                                     andDeviceUID:outputUID
                                                                       withStream:0];
        desc.name = kAppName;
        desc.privateTap = YES;
        desc.muteBehavior = CATapMuted;

        OSStatus err = AudioHardwareCreateProcessTap(desc, &tap);
        if (err != noErr || tap == kAudioObjectUnknown)
        {
            tap = kAudioObjectUnknown;
            return [NSString stringWithFormat:@"Couldn't tap system audio (error %d).", (int)err];
        }

        NSString* tapUID = desc.UUID.UUIDString;

        // the IO callback reads the tap as interleaved 32-bit float: make sure that is what it delivers
        {
            AudioStreamBasicDescription tf = {};
            UInt32 sz = sizeof(tf);
            AudioObjectPropertyAddress fa = addr(kAudioTapPropertyFormat);
            const OSStatus fe = AudioObjectGetPropertyData(tap, &fa, 0, nullptr, &sz, &tf);
            if (fe != noErr || tf.mFormatID != kAudioFormatLinearPCM || !(tf.mFormatFlags & kAudioFormatFlagIsFloat)
                || (tf.mFormatFlags & kAudioFormatFlagIsNonInterleaved) || tf.mBitsPerChannel != 32 || tf.mChannelsPerFrame == 0)
            {
                stop();
                return [NSString stringWithFormat:@"System audio comes in a format the EQ can't read (error %d).", (int)fe];
            }
        }

        NSDictionary* aggDesc = @{
            @kAudioAggregateDeviceNameKey: kAppName,
            @kAudioAggregateDeviceUIDKey: [NSString stringWithFormat:@"com.wake.pump.systemeq.%@", NSUUID.UUID.UUIDString],
            @kAudioAggregateDeviceMainSubDeviceKey: outputUID,
            @kAudioAggregateDeviceIsPrivateKey: @YES,
            @kAudioAggregateDeviceIsStackedKey: @NO,
            @kAudioAggregateDeviceTapAutoStartKey: @YES,
            @kAudioAggregateDeviceSubDeviceListKey: @[ @{ @kAudioSubDeviceUIDKey: outputUID } ],
            @kAudioAggregateDeviceTapListKey: @[ @{ @kAudioSubTapDriftCompensationKey: @YES,
                                                    @kAudioSubTapUIDKey: tapUID } ],
        };

        err = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)aggDesc, &aggregate);
        if (err != noErr || aggregate == kAudioObjectUnknown)
        {
            aggregate = kAudioObjectUnknown;
            stop();
            return [NSString stringWithFormat:@"Couldn't create the audio device (error %d).", (int)err];
        }

        // the aggregate runs at the output's rate
        Float64 aggRate = rate;
        {
            AudioObjectPropertyAddress a = addr(kAudioDevicePropertyNominalSampleRate);
            const OSStatus re = AudioObjectSetPropertyData(aggregate, &a, 0, nullptr, sizeof(aggRate), &aggRate);
            if (re != noErr)
            {
                stop();
                return [NSString stringWithFormat:@"Couldn't run at the output's rate of %.0f Hz (error %d).", rate, (int)re];
            }
        }

        // keep IO slices within what the EQ was told to expect
        {
            UInt32 frames = 512;
            AudioObjectPropertyAddress a = addr(kAudioDevicePropertyBufferFrameSize);
            const OSStatus be = AudioObjectSetPropertyData(aggregate, &a, 0, nullptr, sizeof(frames), &frames);
            if (be != noErr)   // not fatal: the IO callback works through any slice size in kMaxFrames pieces
                NSLog(@"%@: couldn't set the buffer size (error %d); using the device's", kAppName, (int)be);
        }

        if (!prepareUnit(rate))
        {
            stop();
            return @"The EQ could not start.";
        }

        err = AudioDeviceCreateIOProcID(aggregate, ioProcFn, this, &ioProc);
        if (err == noErr && !useOnlyTapInput())
        {
            // starting anyway would read the device's real inputs: the microphone prompt, Bluetooth call quality
            stop();
            return @"Couldn't switch the output device's own inputs off.";
        }
        if (err == noErr)
            err = AudioDeviceStart(aggregate, ioProc);
        if (err != noErr)
        {
            stop();
            return [NSString stringWithFormat:@"Couldn't start audio (error %d).", (int)err];
        }

        running = true;
        paused = false;
        silentFrames = 0;
        return nil;
    }

    // The aggregate's inputs are the output device's own inputs (a microphone, an interface's inputs) followed by
    // the tap. Switch everything but the tap off: reading a real input would ask for microphone access and would put
    // Bluetooth headphones into their low-quality call mode.
    // Returns false when the other inputs couldn't be switched off.
    bool useOnlyTapInput()
    {
        AudioObjectPropertyAddress streamsAddr = addr(kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput);
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize(aggregate, &streamsAddr, 0, nullptr, &size) != noErr)
            return false;
        const UInt32 streams = size / sizeof(AudioStreamID);
        if (streams == 0)
            return false;   // not even the tap
        tapBuffer = streams - 1;   // the tap comes last
        if (streams == 1)
            return true;   // the tap alone: nothing to switch off

        AudioObjectPropertyAddress a = addr(kAudioDevicePropertyIOProcStreamUsage, kAudioObjectPropertyScopeInput);
        size = 0;
        if (AudioObjectGetPropertyDataSize(aggregate, &a, 0, nullptr, &size) != noErr || size == 0)
            return false;
        std::vector<uint8_t> mem(size);
        auto* usage = reinterpret_cast<AudioHardwareIOProcStreamUsage*>(mem.data());
        usage->mIOProc = reinterpret_cast<void*>(ioProc);
        if (AudioObjectGetPropertyData(aggregate, &a, 0, nullptr, &size, usage) != noErr)
            return false;
        for (UInt32 i = 0; i < usage->mNumberStreams; ++i)
            usage->mStreamIsOn[i] = (i == tapBuffer) ? 1 : 0;
        return AudioObjectSetPropertyData(aggregate, &a, 0, nullptr, size, usage) == noErr;
    }

    // stop the device while nothing plays (the tap stays, still muting), and start it again
    void pause()
    {
        if (!running || paused || aggregate == kAudioObjectUnknown || ioProc == nullptr)
            return;
        if (AudioDeviceStop(aggregate, ioProc) == noErr)
            paused = true;
    }

    void resume()
    {
        if (!running || !paused)
            return;
        silentFrames = 0;
        if (AudioDeviceStart(aggregate, ioProc) == noErr)
            paused = false;
    }

    void stop()
    {
        if (aggregate != kAudioObjectUnknown && ioProc != nullptr)
        {
            AudioDeviceStop(aggregate, ioProc);
            AudioDeviceDestroyIOProcID(aggregate, ioProc);
        }
        ioProc = nullptr;

        if (aggregate != kAudioObjectUnknown)
            AudioHardwareDestroyAggregateDevice(aggregate);
        aggregate = kAudioObjectUnknown;

        if (tap != kAudioObjectUnknown)
            AudioHardwareDestroyProcessTap(tap);
        tap = kAudioObjectUnknown;

        running = false;
        paused = false;
    }
};

// is any other process playing (has its output running)?
static bool otherProcessPlaying(std::vector<AudioObjectID>* listOut = nullptr)
{
    AudioObjectPropertyAddress a = addr(kAudioHardwarePropertyProcessObjectList);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr)
        return true;   // can't tell: assume yes, so we never pause on a guess
    std::vector<AudioObjectID> procs(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, procs.data()) != noErr)
        return true;
    procs.resize(size / sizeof(AudioObjectID));
    if (listOut != nullptr)
        *listOut = procs;

    const pid_t me = getpid();
    for (AudioObjectID p : procs)
    {
        pid_t pid = 0;
        UInt32 out = 0;
        if (getProp(p, kAudioProcessPropertyPID, pid) && pid == me)
            continue;
        if (getProp(p, kAudioProcessPropertyIsRunningOutput, out) && out != 0)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// the bundled pump EQ: register the component inside the app for this process only

static AudioComponent registerBundledEQ()
{
    NSString* path = [[NSBundle.mainBundle builtInPlugInsPath]
                      stringByAppendingPathComponent:@"pump-eq.component/Contents/MacOS/pump-eq"];
    void* lib = dlopen(path.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
    {
        NSLog(@"%@: couldn't load %@: %s", kAppName, path, dlerror());
        return nullptr;
    }
    auto factory = reinterpret_cast<AudioComponentFactoryFunction>(dlsym(lib, "PluginAUFactory"));
    if (factory == nullptr)
        return nullptr;

    AudioComponentDescription desc = {};
    desc.componentType = 'aufx';
    desc.componentSubType = 'pmEQ';
    desc.componentManufacturer = 'Wake';
    return AudioComponentRegister(&desc, CFSTR("wake: pump EQ Equalizer"), 0x00010000, factory);
}

// ---------------------------------------------------------------------------------------------------------------------
// the app: status item, EQ window, state

// The plugin's own view inside the AU's container: the one that should get the keyboard. Making the container
// first responder (it accepts, but doesn't pass keys on) left typing in preset names going nowhere.
static NSView* keyViewIn(NSView* v)
{
    for (NSView* sub in v.subviews)   // the outermost one that takes it (pugl's wrapper view, not its GL view)
        if (sub.acceptsFirstResponder) return sub;
    for (NSView* sub in v.subviews)
        if (NSView* deeper = keyViewIn(sub)) return deeper;
    return nil;
}

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, NSMenuDelegate>
- (void)devicesChanged:(BOOL)force;
- (void)processesChanged;
@end

@implementation AppDelegate
{
    Engine engine;
    NSStatusItem* statusItem;
    NSWindow* window;
    NSView* eqView;
    id frameObserver;
    NSString* problem;
    BOOL wantOn;
    NSString* stateKey;                  // whose settings the EQ holds: an output's UID, or nil for "every output"
    std::vector<AudioObjectID> watchedProcesses;
    AudioObjectID listenedOutput;        // the output whose rate / alive flag we listen to
    AudioObjectID failedOutput;          // last start that failed: don't retry until something changes
    Float64 failedRate;
}

static NSUserDefaults* prefs() { return NSUserDefaults.standardUserDefaults; }
static BOOL perDevice() { return ![prefs() boolForKey:@"oneEQ"]; }

static NSURL* stateDir()
{
    NSURL* dir = [[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory
                                                       inDomains:NSUserDomainMask].firstObject
                  URLByAppendingPathComponent:@"wake/System EQ" isDirectory:YES];
    [NSFileManager.defaultManager createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:nil];
    return dir;
}

// one file per output (named from its UID), eq-state.plist for "same EQ on every output", and eq-last.plist (the
// latest settings of any output: where an output seen for the first time starts)
static NSURL* lastStateURL() { return [stateDir() URLByAppendingPathComponent:@"eq-last.plist"]; }

static NSURL* stateURL(NSString* key)
{
    if (key == nil)
        return [stateDir() URLByAppendingPathComponent:@"eq-state.plist"];
    NSMutableString* safe = [NSMutableString string];
    for (NSUInteger i = 0; i < key.length && safe.length < 120; ++i)
    {
        const unichar c = [key characterAtIndex:i];
        [safe appendFormat:@"%C", (unichar)(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) ? c : '_')];
    }
    return [stateDir() URLByAppendingPathComponent:[NSString stringWithFormat:@"eq-%@.plist", safe]];
}

static NSString* currentOutputKey()
{
    return perDevice() ? stringProp(defaultOutputDevice(), kAudioDevicePropertyDeviceUID) : nil;
}

- (void)applicationDidFinishLaunching:(NSNotification*)note
{
    AudioComponent comp = registerBundledEQ();
    if (comp == nullptr || AudioComponentInstanceNew(comp, &engine.au) != noErr)
    {
        engine.au = nullptr;
        problem = @"The bundled pump EQ couldn't be loaded.";
    }
    else
    {
        Float64 rate = 48000.0;
        getProp(defaultOutputDevice(), kAudioDevicePropertyNominalSampleRate, rate);
        engine.prepareUnit(rate);
        stateKey = currentOutputKey();
        if (![self loadStateFor:stateKey] && stateKey != nil)
            [self loadStateFrom:lastStateURL()];   // a new output starts from the latest settings, if any
    }

    statusItem = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
    NSImage* img = [NSImage imageWithSystemSymbolName:@"slider.vertical.3" accessibilityDescription:kAppName];
    [img setTemplate:YES];   // (".template" is a C++ keyword in Objective-C++)
    statusItem.button.image = img;
    statusItem.button.toolTip = kAppName;
    NSMenu* menu = [NSMenu new];
    menu.delegate = self;
    statusItem.menu = menu;

    [self listenForDeviceChanges];
    [self watchProcesses];

    wantOn = ![prefs() boolForKey:@"off"];
    if (wantOn)
        [self startEngine];

    // save now and then; check once a second whether the Mac has gone quiet
    // (common modes: they keep running while the status menu is open)
    NSTimer* save = [NSTimer timerWithTimeInterval:30 repeats:YES block:^(NSTimer*) { [self saveState]; }];
    NSTimer* idle = [NSTimer timerWithTimeInterval:1 repeats:YES block:^(NSTimer*) { [self idleCheck]; }];
    [NSRunLoop.mainRunLoop addTimer:save forMode:NSRunLoopCommonModes];
    [NSRunLoop.mainRunLoop addTimer:idle forMode:NSRunLoopCommonModes];

    if (![prefs() boolForKey:@"launchedBefore"])
    {
        [prefs() setBool:YES forKey:@"launchedBefore"];
        [self showEQ:nil];
    }
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)app hasVisibleWindows:(BOOL)visible
{
    [self showEQ:nil];
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)note
{
    [self saveState];
    engine.stop();
}

// --- engine on/off

- (void)startEngine
{
    if (engine.au == nullptr)
        return;

    // a different output: keep the old one's settings, bring back the new one's
    NSString* key = currentOutputKey();
    if (perDevice() && key != nil && ![key isEqualToString:stateKey ?: @""])
        [self switchStateTo:key];

    problem = engine.start();
    if (problem != nil)
    {
        NSLog(@"%@: %@", kAppName, problem);
        failedOutput = defaultOutputDevice();
        failedRate = 0.0;
        getProp(failedOutput, kAudioDevicePropertyNominalSampleRate, failedRate);
    }
    else
        failedOutput = kAudioObjectUnknown;
    [self listenToOutput:engine.running ? engine.output : kAudioObjectUnknown];
    [self updateStatus];
}

// the output's sample rate and alive flag
- (void)listenToOutput:(AudioObjectID)dev
{
    if (dev == listenedOutput)
        return;
    AudioObjectPropertyAddress rate = addr(kAudioDevicePropertyNominalSampleRate);
    AudioObjectPropertyAddress alive = addr(kAudioDevicePropertyDeviceIsAlive);
    if (listenedOutput != kAudioObjectUnknown)
    {
        AudioObjectRemovePropertyListener(listenedOutput, &rate, deviceListener, (__bridge void*)self);
        AudioObjectRemovePropertyListener(listenedOutput, &alive, deviceListener, (__bridge void*)self);
    }
    listenedOutput = dev;
    if (dev != kAudioObjectUnknown)
    {
        AudioObjectAddPropertyListener(dev, &rate, deviceListener, (__bridge void*)self);
        AudioObjectAddPropertyListener(dev, &alive, deviceListener, (__bridge void*)self);
    }
}

- (void)toggle:(id)sender
{
    wantOn = !wantOn;
    [prefs() setBool:!wantOn forKey:@"off"];
    if (wantOn)
        [self startEngine];
    else
    {
        engine.stop();
        problem = nil;
        [self listenToOutput:kAudioObjectUnknown];
        [self updateStatus];
    }
}

- (void)updateStatus
{
    statusItem.button.appearsDisabled = !engine.running;
    NSString* state = engine.running ? @"on" : @"off";
    if (perDevice())
    {
        NSString* name = stringProp(defaultOutputDevice(), kAudioObjectPropertyName) ?: @"output";
        window.title = [NSString stringWithFormat:@"System EQ — %@ — %@", name, state];
    }
    else
        window.title = [NSString stringWithFormat:@"System EQ — every output — %@", state];
}

// restart on a new default output, a sample-rate change or a device going away
static OSStatus deviceListener(AudioObjectID obj, UInt32 count, const AudioObjectPropertyAddress* addrs, void* ctx)
{
    AppDelegate* app = (__bridge AppDelegate*)ctx;
    bool force = false;   // the audio service restarted or the output died: our tap and device are gone
    for (UInt32 i = 0; i < count; ++i)
        if (addrs[i].mSelector == kAudioHardwarePropertyServiceRestarted ||
            addrs[i].mSelector == kAudioDevicePropertyDeviceIsAlive)
            force = true;
    dispatch_async(dispatch_get_main_queue(), ^{ [app devicesChanged:force]; });
    return noErr;
}

- (void)listenForDeviceChanges
{
    AudioObjectPropertyAddress a = addr(kAudioHardwarePropertyDefaultOutputDevice);
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &a, deviceListener, (__bridge void*)self);
    AudioObjectPropertyAddress d = addr(kAudioHardwarePropertyDevices);
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &d, deviceListener, (__bridge void*)self);
    AudioObjectPropertyAddress r = addr(kAudioHardwarePropertyServiceRestarted);
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &r, deviceListener, (__bridge void*)self);
}

- (void)devicesChanged:(BOOL)force
{
    if (!wantOn)
    {
        [self updateStatus];
        return;
    }
    // our own aggregate appearing/disappearing also changes the device list: only restart for real changes
    Float64 rate = 0.0;
    const AudioObjectID out = defaultOutputDevice();
    getProp(out, kAudioDevicePropertyNominalSampleRate, rate);
    if (!force)
    {
        if (engine.running && out == engine.output && rate == engine.outputRate)
            return;
        if (!engine.running && out == failedOutput && rate == failedRate)
            return;   // the same setup failed already: wait for something to change
    }
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(startEngine) object:nil];
    [self performSelector:@selector(startEngine) withObject:nil afterDelay:0.3 inModes:@[ NSRunLoopCommonModes ]];
}

// --- idle: stop the audio device while nothing plays, start it the moment something does

static OSStatus processListener(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void* ctx)
{
    AppDelegate* app = (__bridge AppDelegate*)ctx;
    dispatch_async(dispatch_get_main_queue(), ^{ [app processesChanged]; });
    return noErr;
}

- (void)watchProcesses
{
    AudioObjectPropertyAddress list = addr(kAudioHardwarePropertyProcessObjectList);
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &list, processListener, (__bridge void*)self);
    [self processesChanged];
}

- (void)processesChanged
{
    // listen to every process's "output running" flag (new processes appear in the list)
    std::vector<AudioObjectID> procs;
    const bool playing = otherProcessPlaying(&procs);
    AudioObjectPropertyAddress running = addr(kAudioProcessPropertyIsRunningOutput);
    for (AudioObjectID p : procs)
        if (std::find(watchedProcesses.begin(), watchedProcesses.end(), p) == watchedProcesses.end())
        {
            AudioObjectAddPropertyListener(p, &running, processListener, (__bridge void*)self);
            watchedProcesses.push_back(p);
        }
    // forget processes that are gone
    watchedProcesses.erase(std::remove_if(watchedProcesses.begin(), watchedProcesses.end(), [&](AudioObjectID p) {
        return std::find(procs.begin(), procs.end(), p) == procs.end();
    }), watchedProcesses.end());

    if (playing && engine.paused)
        engine.resume();
}

- (void)idleCheck
{
    if (engine.running && engine.paused && otherProcessPlaying())
        engine.resume();   // fallback if a resume was missed or failed
    if (!engine.running || engine.paused || engine.auRate <= 0.0)
        return;
    // 5 s of silence and no app with its output running: nothing can start without us hearing about it
    if (engine.silentFrames.load() > (uint64_t)(5.0 * engine.auRate) && !otherProcessPlaying())
        engine.pause();
}

// --- menu

- (void)menuNeedsUpdate:(NSMenu*)menu
{
    [menu removeAllItems];

    NSMenuItem* on = [menu addItemWithTitle:@"EQ On" action:@selector(toggle:) keyEquivalent:@""];
    on.target = self;
    on.state = wantOn ? NSControlStateValueOn : NSControlStateValueOff;

    NSMenuItem* show = [menu addItemWithTitle:@"Show EQ…" action:@selector(showEQ:) keyEquivalent:@""];
    show.target = self;

    [menu addItem:NSMenuItem.separatorItem];

    NSString* outName = stringProp(defaultOutputDevice(), kAudioObjectPropertyName) ?: @"none";
    NSString* info = engine.running
        ? [NSString stringWithFormat:@"Output: %@ · %.1f kHz%@", outName, engine.auRate / 1000.0,
                                     engine.paused ? @" · idle" : @""]
        : [NSString stringWithFormat:@"Output: %@", outName];
    [menu addItemWithTitle:info action:nil keyEquivalent:@""].enabled = NO;
    if (problem != nil)
        [menu addItemWithTitle:problem action:nil keyEquivalent:@""].enabled = NO;
    else if (engine.running && !engine.paused && engine.auRate > 0.0 &&
             engine.silentFrames.load() > (uint64_t)(10.0 * engine.auRate) && otherProcessPlaying())
        [menu addItemWithTitle:@"Hearing nothing? Check the permission below" action:nil keyEquivalent:@""].enabled = NO;

    NSMenuItem* each = [menu addItemWithTitle:@"Separate EQ for Each Output" action:@selector(togglePerDevice:)
                                keyEquivalent:@""];
    each.target = self;
    each.state = perDevice() ? NSControlStateValueOn : NSControlStateValueOff;

    [menu addItem:NSMenuItem.separatorItem];

    NSMenuItem* login = [menu addItemWithTitle:@"Open at Login" action:@selector(toggleLogin:) keyEquivalent:@""];
    login.target = self;
    login.state = SMAppService.mainAppService.status == SMAppServiceStatusEnabled ? NSControlStateValueOn
                                                                                  : NSControlStateValueOff;

    NSMenuItem* privacy = [menu addItemWithTitle:@"Audio Recording Permission…" action:@selector(openPrivacy:)
                                   keyEquivalent:@""];
    privacy.target = self;

    [menu addItem:NSMenuItem.separatorItem];
    NSMenuItem* quit = [menu addItemWithTitle:@"Quit System EQ" action:@selector(terminate:) keyEquivalent:@"q"];
    quit.target = NSApp;
}

- (void)togglePerDevice:(id)sender
{
    [self saveState];
    [prefs() setBool:perDevice() forKey:@"oneEQ"];   // flips it
    // the current settings carry over into the new mode when it has none saved yet
    NSString* key = currentOutputKey();
    stateKey = key;
    if ([self loadStateFor:key])
        [self reloadView];
    else
        [self saveState];
    [self updateStatus];
}

- (void)toggleLogin:(id)sender
{
    NSError* err = nil;
    SMAppService* svc = SMAppService.mainAppService;
    if (svc.status == SMAppServiceStatusEnabled)
        [svc unregisterAndReturnError:&err];
    else
        [svc registerAndReturnError:&err];
    if (err != nil)
        NSLog(@"%@: login item: %@", kAppName, err);
}

- (void)openPrivacy:(id)sender
{
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:
        @"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
}

// --- the EQ window (the plugin's own view)

- (NSView*)makeView
{
    AudioUnitCocoaViewInfo info = {};
    UInt32 size = sizeof(info);
    if (AudioUnitGetProperty(engine.au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &info, &size) != noErr)
        return nil;

    NSURL* bundleURL = (__bridge_transfer NSURL*)info.mCocoaAUViewBundleLocation;
    NSString* className = (__bridge_transfer NSString*)info.mCocoaAUViewClass[0];
    NSBundle* bundle = [NSBundle bundleWithURL:bundleURL];
    Class cls = [bundle classNamed:className] ?: NSClassFromString(className);
    id<AUCocoaUIBase> factory = [cls new];
    if (factory == nil)
        return nil;
    return [factory uiViewForAudioUnit:engine.au withSize:NSMakeSize(1040, 660)];
}

- (void)attachView:(NSView*)view
{
    if (frameObserver != nil)
        [NSNotificationCenter.defaultCenter removeObserver:frameObserver];
    frameObserver = nil;

    // the plugin decides its size: setting the content view would stretch it to the window's old size
    const NSSize want = view.frame.size;
    eqView = view;
    window.contentView = view;
    [window setContentSize:want];
    [view setFrameSize:want];

    // the plugin resizes its own view (corner drag, scale): follow it
    view.postsFrameChangedNotifications = YES;
    __weak AppDelegate* weakSelf = self;
    frameObserver = [NSNotificationCenter.defaultCenter addObserverForName:NSViewFrameDidChangeNotification
                                                                    object:view
                                                                     queue:nil
                                                                usingBlock:^(NSNotification*) {
        AppDelegate* me = weakSelf;
        if (me == nil || me->window == nil || me->eqView == nil)
            return;
        NSRect content = [me->window contentRectForFrameRect:me->window.frame];
        const NSSize want = me->eqView.frame.size;
        if (NSEqualSizes(content.size, want))
            return;
        NSRect frame = [me->window frameRectForContentRect:
            NSMakeRect(content.origin.x, NSMaxY(content) - want.height, want.width, want.height)];
        [me->window setFrame:frame display:YES];
    }];
    [window makeFirstResponder:keyViewIn(view) ?: view];
}

// the view reads its state when it opens: after loading other settings, open a fresh one
- (void)reloadView
{
    if (window == nil || eqView == nil)
        return;
    NSView* view = [self makeView];
    if (view != nil)
        [self attachView:view];
}

- (void)showEQ:(id)sender
{
    if (engine.au == nullptr)
        return;

    if (window == nil)
    {
        window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1040, 660)
                                             styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                       NSWindowStyleMaskMiniaturizable
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        window.releasedWhenClosed = NO;
        window.delegate = self;
        if (![window setFrameUsingName:@"SystemEQ"])
            [window center];
        [window setFrameAutosaveName:@"SystemEQ"];   // remembers where the window was (the size is the plugin's)
    }

    // the view is released while the window is closed (so the plugin's UI isn't redrawing for nobody)
    if (eqView == nil)
    {
        NSView* view = [self makeView];
        if (view == nil)
            return;
        const NSPoint topLeft = NSMakePoint(NSMinX(window.frame), NSMaxY(window.frame));
        [self attachView:view];
        [window setFrameTopLeftPoint:topLeft];
    }
    [self updateStatus];

    [NSApp activateIgnoringOtherApps:YES];
    [window makeKeyAndOrderFront:nil];
    [window makeFirstResponder:keyViewIn(eqView) ?: eqView];
}

- (void)windowWillClose:(NSNotification*)note
{
    [self saveState];
    if (frameObserver != nil)
        [NSNotificationCenter.defaultCenter removeObserver:frameObserver];
    frameObserver = nil;
    window.contentView = [[NSView alloc] initWithFrame:NSZeroRect];
    eqView = nil;
}

// --- state: the EQ's full state per output (or one for every output), kept between launches

- (void)saveState
{
    if (engine.au == nullptr)
        return;
    CFPropertyListRef plist = nullptr;
    UInt32 size = sizeof(plist);
    if (AudioUnitGetProperty(engine.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, &size)
        != noErr || plist == nullptr)
        return;
    NSData* data = [NSPropertyListSerialization dataWithPropertyList:(__bridge id)plist
                                                              format:NSPropertyListBinaryFormat_v1_0
                                                             options:0
                                                               error:nil];
    CFRelease(plist);
    [data writeToURL:stateURL(stateKey) atomically:YES];
    if (stateKey != nil)
        [data writeToURL:lastStateURL() atomically:YES];   // the starting point for outputs not seen yet
}

- (BOOL)loadStateFor:(NSString*)key
{
    return [self loadStateFrom:stateURL(key)];
}

- (BOOL)loadStateFrom:(NSURL*)url
{
    NSData* data = [NSData dataWithContentsOfURL:url];
    if (data == nil)
        return NO;
    id plist = [NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:nil];
    if (plist == nil)
        return NO;
    CFPropertyListRef ref = (__bridge CFPropertyListRef)plist;
    return AudioUnitSetProperty(engine.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &ref,
                                sizeof(ref)) == noErr;
}

- (void)switchStateTo:(NSString*)key
{
    [self saveState];                 // the old output keeps what it had
    const BOOL loaded = [self loadStateFor:key];
    stateKey = key;
    if (loaded)
        [self reloadView];
    else
        [self saveState];             // an output seen for the first time starts with the current settings
}

@end

int main(int argc, const char* argv[])
{
    @autoreleasepool
    {
        NSApplication* app = NSApplication.sharedApplication;
        app.activationPolicy = NSApplicationActivationPolicyAccessory;
        AppDelegate* delegate = [AppDelegate new];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
