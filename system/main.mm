// System EQ: pump EQ (and pump ROOM's room correction) on everything the Mac plays.
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
//               -> IOProc -> pump EQ (AudioUnitRender) -> pump ROOM (when Room Correction is on) -> output device
//
// pump ROOM (Contents/PlugIns/pump-room.component, when the app was built with it) applies a room profile made with
// the pump ROOM app; like the EQ it keeps its settings (and profile) per output. Without a profile it passes the sound
// through. Turning correction off or switching profiles never gets louder: everything plays at the lowest preamp of
// the profiles (the level hold), and only quitting gives the full level back. Profiles kept in the app's library (Application Support/wake/System EQ/Profiles) are listed in the menu,
// room profiles and headphone profiles apart, to switch between with one click; opening a .pumproom file (a double
// click) adds it to the library and switches to it.
//
// Nothing on the audio thread allocates, locks or calls Objective-C.

#import <Cocoa/Cocoa.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <ServiceManagement/ServiceManagement.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
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
    AudioUnit room = nullptr;               // pump ROOM after the EQ (null when the app was built without it)
    double roomRate = 0.0;                  // the rate it is initialized at (0: not usable)
    std::atomic<bool> roomOn { true };      // Room Correction in the menu

    AudioObjectID tap = kAudioObjectUnknown;
    AudioObjectID aggregate = kAudioObjectUnknown;
    AudioObjectID output = kAudioObjectUnknown;
    AudioDeviceIOProcID ioProc = nullptr;
    UInt32 outLeft = 0, outRight = 1;     // 0-based output channels (the device's preferred stereo pair)
    UInt32 tapBuffer = 0;                 // index of the tap's buffer in the aggregate's input list
    double outputRate = 0.0;              // the output device's rate when we started

    // audio-thread buffers (allocated once)
    std::vector<float> inL, inR, outL, outR, midL, midR, trimL, trimR;   // mid: the EQ's output, ROOM's input
    // The level hold: a gain after the room stage, so that turning Room Correction off, choosing No Profile or
    // switching profiles never makes the Mac louder. Every state plays at the lowest preamp held (the most a profile
    // has cut); only quitting gives the full level back. Set from the main thread; ramped here.
    std::atomic<float> trimTarget { 1.0f };
    float trimNow = 1.0f;
    Float64 sampleTime = 0.0;
    std::atomic<bool> running { false };   // tap + device exist (EQ on)
    bool paused = false;                   // device stopped while nothing plays (main thread)

    // frames of continuous silence at the tap (written by the audio thread, read by the main thread)
    std::atomic<uint64_t> silentFrames { 0 };

    Engine() : inL(kMaxFrames), inR(kMaxFrames), outL(kMaxFrames), outR(kMaxFrames), midL(kMaxFrames), midR(kMaxFrames),
               trimL(kMaxFrames), trimR(kMaxFrames) {}

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

    // --- ROOM's input: what the EQ just produced
    static OSStatus renderRoomInput(void* refCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32,
                                    UInt32 frames, AudioBufferList* io)
    {
        Engine* e = static_cast<Engine*>(refCon);
        const float* src[2] = { e->midL.data(), e->midR.data() };
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

            const float* resL = static_cast<float*>(out.list.mBuffers[0].mData);
            const float* resR = static_cast<float*>(out.second.mData);
            if (e->room != nullptr && e->roomRate == e->auRate && e->roomOn.load(std::memory_order_relaxed))
            {
                std::memcpy(e->midL.data(), resL, n * sizeof(float));
                std::memcpy(e->midR.data(), resR, n * sizeof(float));
                struct { AudioBufferList list; AudioBuffer second; } ro;
                ro.list.mNumberBuffers = 2;
                ro.list.mBuffers[0] = { 1, n * (UInt32)sizeof(float), e->outL.data() };
                ro.second = { 1, n * (UInt32)sizeof(float), e->outR.data() };
                AudioUnitRenderActionFlags rflags = 0;
                if (AudioUnitRender(e->room, &rflags, &ts, 0, n, &ro.list) == noErr)
                {
                    resL = static_cast<float*>(ro.list.mBuffers[0].mData);
                    resR = static_cast<float*>(ro.second.mData);
                }
                else
                {
                    resL = e->midL.data();   // the EQ's output rather than dropping out
                    resR = e->midR.data();
                }
            }

            // the level hold (a gain ramped toward its target so a change never clicks)
            {
                const float target = e->trimTarget.load(std::memory_order_relaxed);
                float g = e->trimNow;
                if (g != 1.0f || target != 1.0f)
                {
                    // down fast (5 ms: a quieter state never lets a louder one through), up slowly (50 ms)
                    const float fs = (float)(e->auRate > 0.0 ? e->auRate : 48000.0);
                    const float down = 1.0f / (0.005f * fs), up = 1.0f / (0.05f * fs);
                    for (UInt32 i = 0; i < n; ++i)
                    {
                        if (g < target) g = std::min(target, g + up * std::max(g, 0.05f));
                        else if (g > target) g = std::max(target, g - down * g);
                        e->trimL[i] = resL[i] * g;
                        e->trimR[i] = resR[i] * g;
                    }
                    e->trimNow = g;
                    resL = e->trimL.data();
                    resR = e->trimR.data();
                }
            }

            writeChannel(outOutput, e->outLeft, resL, n, done);
            writeChannel(outOutput, e->outRight, resR, n, done);
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
        {
            prepareRoom(rate);
            return true;
        }

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
        prepareRoom(rate);
        return true;
    }

    // --- ROOM the same way (its failing only leaves Room Correction out)
    void prepareRoom(double rate)
    {
        if (room == nullptr || rate == roomRate)
            return;
        AudioUnitUninitialize(room);
        roomRate = 0.0;
        AudioStreamBasicDescription fmt = {};
        fmt.mSampleRate = rate;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
        fmt.mBytesPerPacket = sizeof(float);
        fmt.mFramesPerPacket = 1;
        fmt.mBytesPerFrame = sizeof(float);
        fmt.mChannelsPerFrame = 2;
        fmt.mBitsPerChannel = 32;
        UInt32 maxFrames = kMaxFrames;
        AURenderCallbackStruct cb = { renderRoomInput, this };
        OSStatus st = AudioUnitSetProperty(room, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
        if (st == noErr)
            st = AudioUnitSetProperty(room, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, sizeof(fmt));
        if (st == noErr)
            st = AudioUnitSetProperty(room, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
                                      &maxFrames, sizeof(maxFrames));
        if (st == noErr)
            st = AudioUnitSetProperty(room, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
        if (st != noErr || AudioUnitInitialize(room) != noErr)
        {
            NSLog(@"%@: room correction could not start at %.0f Hz (error %d)", kAppName, rate, (int)st);
            return;
        }
        roomRate = rate;
    }

    // --- start: tap everything but this process, build the aggregate, run (main thread)
    NSString* start()
    {
        trimNow = trimTarget.load();   // start at the held level, not full level
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

// pump ROOM, the same way (optional: an app built without it has no Room Correction)
static AudioComponent registerBundledRoom()
{
    NSString* path = [[NSBundle.mainBundle builtInPlugInsPath]
                      stringByAppendingPathComponent:@"pump-room.component/Contents/MacOS/pump-room"];
    if (![NSFileManager.defaultManager fileExistsAtPath:path])
        return nullptr;
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
    desc.componentSubType = 'pmRM';
    desc.componentManufacturer = 'Wake';
    return AudioComponentRegister(&desc, CFSTR("wake: pump ROOM Room Correction"), 0x00010000, factory);
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
    NSWindow* roomWindow;                // pump ROOM's own window
    NSView* roomView;
    id roomFrameObserver;
    NSString* problem;
    BOOL wantOn;
    NSString* stateKey;                  // whose settings the EQ holds: an output's UID, or nil for "every output"
    std::vector<AudioObjectID> watchedProcesses;
    AudioObjectID listenedOutput;        // the output whose rate / alive flag we listen to
    AudioObjectID failedOutput;          // last start that failed: don't retry until something changes
    unsigned levelTicks;                 // idle-check count (the level hold is refreshed every other one)
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

static NSURL* roomLastStateURL() { return [stateDir() URLByAppendingPathComponent:@"room-last.plist"]; }

// "eq-..." for the EQ, "room-..." for room correction (the same keys)
static NSURL* stateURLFor(NSString* prefix, NSString* key)
{
    if (key == nil)
        return [stateDir() URLByAppendingPathComponent:[NSString stringWithFormat:@"%@-state.plist", prefix]];
    NSMutableString* safe = [NSMutableString string];
    for (NSUInteger i = 0; i < key.length && safe.length < 120; ++i)
    {
        const unichar c = [key characterAtIndex:i];
        [safe appendFormat:@"%C", (unichar)(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) ? c : '_')];
    }
    return [stateDir() URLByAppendingPathComponent:[NSString stringWithFormat:@"%@-%@.plist", prefix, safe]];
}

static NSURL* stateURL(NSString* key) { return stateURLFor(@"eq", key); }
static NSURL* roomStateURL(NSString* key) { return stateURLFor(@"room", key); }

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
        AudioComponent roomComp = registerBundledRoom();
        if (roomComp == nullptr || AudioComponentInstanceNew(roomComp, &engine.room) != noErr)
            engine.room = nullptr;
        engine.roomOn = ![prefs() boolForKey:@"roomOff"];
        Float64 rate = 48000.0;
        getProp(defaultOutputDevice(), kAudioDevicePropertyNominalSampleRate, rate);
        engine.prepareUnit(rate);
        stateKey = currentOutputKey();
        if (![self loadStateFor:stateKey] && stateKey != nil)
        {
            [self loadStateFrom:lastStateURL()];   // a new output starts from the latest settings, if any
            [self loadRoomStateFrom:roomLastStateURL()];
        }
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

    [self updateLevelHold];
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
    // turning System EQ off removes the level hold too: say so when that makes the Mac louder
    const double held = std::min(0.0, [prefs() doubleForKey:@"heldLevelDb"]);
    if (wantOn && held < -0.5)
    {
        NSAlert* a = [NSAlert new];
        a.messageText = [NSString stringWithFormat:@"Turning the EQ off plays %.1f dB louder", -held];
        a.informativeText = @"System EQ holds the level down so room correction never clips. With the EQ off, the Mac plays at "
                            @"full level. Turn your speakers down first, or keep the EQ on and choose No Profile to hear "
                            @"without correction at the same level.";
        [a addButtonWithTitle:@"Turn Off"];
        [a addButtonWithTitle:@"Cancel"];
        [NSApp activateIgnoringOtherApps:YES];
        if ([a runModal] != NSAlertFirstButtonReturn)
            return;
    }
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
        roomWindow.title = [NSString stringWithFormat:@"Room Correction — %@", name];
    }
    else
    {
        window.title = [NSString stringWithFormat:@"System EQ — every output — %@", state];
        roomWindow.title = @"Room Correction — every output";
    }
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
    // a profile loaded in the Room Correction window, or another output's settings: keep the hold current
    if (++levelTicks % 2 == 0)
        [self updateLevelHold];
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

    if (engine.room != nullptr)
    {
        NSMenuItem* roomOn = [menu addItemWithTitle:@"Room Correction" action:@selector(toggleRoom:) keyEquivalent:@""];
        roomOn.target = self;
        roomOn.state = engine.roomOn.load() ? NSControlStateValueOn : NSControlStateValueOff;
        NSMenuItem* rooms = [menu addItemWithTitle:@"Room Profiles" action:nil keyEquivalent:@""];
        rooms.submenu = [self profileMenu:NO];
        NSMenuItem* phones = [menu addItemWithTitle:@"Headphones" action:nil keyEquivalent:@""];
        phones.submenu = [self profileMenu:YES];
        NSMenuItem* roomShow = [menu addItemWithTitle:@"Show Room Correction…" action:@selector(showRoom:)
                                        keyEquivalent:@""];
        roomShow.target = self;
    }

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

// --- the profile library: .pumproom files in Application Support/wake/System EQ/Profiles, listed in the menu (room
// profiles and headphone profiles apart). Choosing one puts its text into pump ROOM's "profile" state for the current
// output (each output remembers its own), so switching speakers, amps or headphones is one click.

static NSURL* profilesDir()
{
    NSURL* dir = [stateDir() URLByAppendingPathComponent:@"Profiles" isDirectory:YES];
    [NSFileManager.defaultManager createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:nil];
    return dir;
}

// a profile file's name and kind from its first lines (nil: not a profile)
static NSDictionary* profileInfo(NSURL* url)
{
    NSFileHandle* fh = [NSFileHandle fileHandleForReadingFromURL:url error:nil];
    if (fh == nil)
        return nil;
    NSData* head = [fh readDataOfLength:4096];
    [fh closeFile];
    NSString* text = [[NSString alloc] initWithData:head encoding:NSUTF8StringEncoding];
    if (text == nil)
        text = [[NSString alloc] initWithData:head encoding:NSISOLatin1StringEncoding];
    if (text == nil)
        return nil;
    NSString* name = nil;
    BOOL headphones = NO, header = NO;
    double preamp = 0.0;
    for (NSString* raw in [text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet])
    {
        NSString* line = [raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        if (line.length == 0 || [line hasPrefix:@"#"])
            continue;
        if (!header)
        {
            if (![line hasPrefix:@"pump-room-profile"])
                return nil;
            header = YES;
            continue;
        }
        if ([line hasPrefix:@"name "])
            name = [line substringFromIndex:5];
        else if ([line hasPrefix:@"kind "])
            headphones = [[line substringFromIndex:5] hasPrefix:@"headphones"];
        else if ([line hasPrefix:@"preamp "])
            preamp = std::min(0.0, std::max(-60.0, [[line substringFromIndex:7] doubleValue]));
        else if ([line hasPrefix:@"channel "])
            break;   // the header lines come before the curves
    }
    if (!header)
        return nil;
    if (name.length == 0)
        name = url.URLByDeletingPathExtension.lastPathComponent;
    return @{ @"url": url, @"name": name, @"headphones": @(headphones), @"preamp": @(preamp) };
}

- (NSArray<NSDictionary*>*)libraryProfiles
{
    NSArray<NSURL*>* files = [NSFileManager.defaultManager contentsOfDirectoryAtURL:profilesDir()
                                                         includingPropertiesForKeys:nil
                                                                            options:NSDirectoryEnumerationSkipsHiddenFiles
                                                                              error:nil];
    NSMutableArray* out = [NSMutableArray array];
    for (NSURL* f in files)
        if ([f.pathExtension.lowercaseString isEqualToString:@"pumproom"])
            if (NSDictionary* info = profileInfo(f))
                [out addObject:info];
    [out sortUsingComparator:^NSComparisonResult(NSDictionary* a, NSDictionary* b) {
        return [a[@"name"] localizedStandardCompare:b[@"name"]];
    }];
    return out;
}

// pump ROOM's saved state: its ClassInfo dictionary, whose data holds "states" (one {key: value} per state)
- (NSMutableDictionary*)roomClassInfo
{
    if (engine.room == nullptr)
        return nil;
    CFPropertyListRef plist = nullptr;
    UInt32 size = sizeof(plist);
    if (AudioUnitGetProperty(engine.room, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, &size)
        != noErr || plist == nullptr)
        return nil;
    id obj = CFBridgingRelease(plist);
    return [obj isKindOfClass:NSDictionary.class] ? [obj mutableCopy] : nil;
}

- (NSString*)roomState:(NSString*)key
{
    NSDictionary* data = [self roomClassInfo][@"data"];
    if (![data isKindOfClass:NSDictionary.class])
        return nil;
    for (NSDictionary* st in data[@"states"])
        if ([st isKindOfClass:NSDictionary.class] && [st[key] isKindOfClass:NSString.class])
            return st[key];
    return nil;
}

- (BOOL)setRoomStates:(NSDictionary<NSString*, NSString*>*)values
{
    NSMutableDictionary* cls = [self roomClassInfo];
    if (cls == nil)
        return NO;
    NSMutableDictionary* data = [cls[@"data"] isKindOfClass:NSDictionary.class] ? [cls[@"data"] mutableCopy]
                                                                                : [NSMutableDictionary dictionary];
    NSMutableArray* states = [NSMutableArray array];
    for (NSDictionary* st in data[@"states"])
        if ([st isKindOfClass:NSDictionary.class] && values[st.allKeys.firstObject] == nil)
            [states addObject:st];
    [values enumerateKeysAndObjectsUsingBlock:^(NSString* k, NSString* v, BOOL*) { [states addObject:@{ k: v }]; }];
    data[@"states"] = states;
    cls[@"data"] = data;
    CFPropertyListRef ref = (__bridge CFPropertyListRef)cls;
    return AudioUnitSetProperty(engine.room, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &ref,
                                sizeof(ref)) == noErr;
}

// the preamp (dB, <= 0) of a profile's text; 0 when it has none
static double preampOf(NSString* text)
{
    for (NSString* raw in [text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet])
    {
        NSString* line = [raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        if ([line hasPrefix:@"preamp "])
            return std::min(0.0, std::max(-60.0, [[line substringFromIndex:7] doubleValue]));
        if ([line hasPrefix:@"channel "])
            break;
    }
    return 0.0;
}

// The level everything plays at: the lowest preamp held so far (kept in the preferences, so it never rises by
// itself), including every profile in the library and the one loaded now. Playing with a profile, the trim makes up
// the difference between its own preamp and the held level; with none, or Room Correction off, the trim is the held
// level itself. Quitting the app removes the tap, and with it the hold.
- (void)updateLevelHold
{
    double held = std::min(0.0, [prefs() doubleForKey:@"heldLevelDb"]);
    for (NSDictionary* p in [self libraryProfiles])
        held = std::min(held, [p[@"preamp"] doubleValue]);
    NSString* text = [self roomState:@"profile"] ?: @"";
    const BOOL active = text.length > 0 && engine.room != nullptr;
    const double own = active ? preampOf(text) : 0.0;
    held = std::min(held, own);
    [prefs() setDouble:held forKey:@"heldLevelDb"];
    const double trimDb = (active && engine.roomOn.load()) ? held - own : held;
    engine.trimTarget = (float)std::pow(10.0, trimDb / 20.0);
}

- (void)resetLevelHold:(id)sender
{
    NSAlert* a = [NSAlert new];
    a.messageText = @"Reset the level hold?";
    a.informativeText = @"The level is held at the lowest preamp of your profiles, so turning correction off or switching "
                        @"profiles never gets louder. Resetting works it out again from the profiles in your library, which "
                        @"can make playback louder. Turn your speakers down first.";
    [a addButtonWithTitle:@"Reset"];
    [a addButtonWithTitle:@"Cancel"];
    [NSApp activateIgnoringOtherApps:YES];
    if ([a runModal] != NSAlertFirstButtonReturn)
        return;
    [prefs() setDouble:0.0 forKey:@"heldLevelDb"];
    [self updateLevelHold];
}

- (NSMenu*)profileMenu:(BOOL)headphones
{
    NSMenu* m = [NSMenu new];
    m.autoenablesItems = NO;
    NSString* current = [self roomState:@"profile_file"] ?: @"";
    NSString* text = [self roomState:@"profile"] ?: @"";
    NSUInteger n = 0;
    for (NSDictionary* p in [self libraryProfiles])
    {
        if ([p[@"headphones"] boolValue] != headphones)
            continue;
        NSMenuItem* it = [m addItemWithTitle:p[@"name"] action:@selector(chooseProfile:) keyEquivalent:@""];
        it.target = self;
        it.representedObject = [p[@"url"] path];
        it.state = [current isEqualToString:[p[@"url"] path]] && text.length > 0 ? NSControlStateValueOn
                                                                                  : NSControlStateValueOff;
        ++n;
    }
    if (n == 0)
        [m addItemWithTitle:headphones ? @"No headphone profiles yet" : @"No room profiles yet" action:nil
              keyEquivalent:@""].enabled = NO;
    [m addItem:NSMenuItem.separatorItem];
    NSMenuItem* none = [m addItemWithTitle:@"No Profile" action:@selector(clearProfile:) keyEquivalent:@""];
    none.target = self;
    none.enabled = text.length > 0;
    NSMenuItem* add = [m addItemWithTitle:@"Add Profiles…" action:@selector(addProfiles:) keyEquivalent:@""];
    add.target = self;
    NSMenuItem* show = [m addItemWithTitle:@"Show Profiles in Finder" action:@selector(showProfiles:) keyEquivalent:@""];
    show.target = self;
    [m addItem:NSMenuItem.separatorItem];
    const double held = std::min(0.0, [prefs() doubleForKey:@"heldLevelDb"]);
    [m addItemWithTitle:[NSString stringWithFormat:@"Level held at %.1f dB (quit for full level)", held] action:nil
          keyEquivalent:@""].enabled = NO;
    NSMenuItem* reset = [m addItemWithTitle:@"Reset Level Hold…" action:@selector(resetLevelHold:) keyEquivalent:@""];
    reset.target = self;
    reset.enabled = held < 0.0;
    return m;
}

// put a profile file's text into pump ROOM for the current output, and turn Room Correction on
- (BOOL)applyProfileAt:(NSURL*)url
{
    if (engine.room == nullptr)
        return NO;
    NSString* text = [NSString stringWithContentsOfURL:url encoding:NSUTF8StringEncoding error:nil];
    if (text == nil || profileInfo(url) == nil)
    {
        NSLog(@"%@: not a pump ROOM profile: %@", kAppName, url.path);
        return NO;
    }
    if (![self setRoomStates:@{ @"profile": text, @"profile_file": url.path }])
        return NO;
    engine.roomOn = true;
    [prefs() setBool:NO forKey:@"roomOff"];
    [self updateLevelHold];
    [self saveState];
    [self reloadView];
    return YES;
}

- (void)chooseProfile:(NSMenuItem*)item
{
    if (NSString* path = item.representedObject)
        [self applyProfileAt:[NSURL fileURLWithPath:path]];
}

- (void)clearProfile:(id)sender
{
    [self setRoomStates:@{ @"profile": @"", @"profile_file": @"" }];
    [self updateLevelHold];
    [self saveState];
    [self reloadView];
}

// copy profile files into the library (a file of the same name is replaced); returns the copies
- (NSArray<NSURL*>*)addToLibrary:(NSArray<NSURL*>*)urls
{
    NSMutableArray* added = [NSMutableArray array];
    NSURL* dir = profilesDir();
    for (NSURL* src in urls)
    {
        if (profileInfo(src) == nil)
        {
            NSLog(@"%@: not a pump ROOM profile: %@", kAppName, src.path);
            continue;
        }
        NSURL* dst = [dir URLByAppendingPathComponent:src.lastPathComponent];
        if ([dst.URLByStandardizingPath.path isEqualToString:src.URLByStandardizingPath.path])
        {
            [added addObject:dst];   // already in the library
            continue;
        }
        [NSFileManager.defaultManager removeItemAtURL:dst error:nil];
        NSError* err = nil;
        if ([NSFileManager.defaultManager copyItemAtURL:src toURL:dst error:&err])
            [added addObject:dst];
        else
            NSLog(@"%@: couldn't add %@: %@", kAppName, src.path, err);
    }
    return added;
}

- (void)addProfiles:(id)sender
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowsMultipleSelection = YES;
    panel.canChooseDirectories = NO;
    panel.message = @"Profiles saved by pump ROOM (.pumproom): room and headphone profiles";
    if (UTType* t = [UTType typeWithFilenameExtension:@"pumproom"])
        panel.allowedContentTypes = @[ t ];
    [NSApp activateIgnoringOtherApps:YES];
    if ([panel runModal] != NSModalResponseOK)
        return;
    NSArray<NSURL*>* added = [self addToLibrary:panel.URLs];
    if (added.count == 1)
        [self applyProfileAt:added.firstObject];   // one file: switch to it as well
}

- (void)showProfiles:(id)sender
{
    [NSWorkspace.sharedWorkspace openURL:profilesDir()];
}

// a .pumproom file opened (double-clicked, or dropped on the app): into the library, and switched to
- (void)application:(NSApplication*)app openURLs:(NSArray<NSURL*>*)urls
{
    NSArray<NSURL*>* added = [self addToLibrary:urls];
    if (added.count > 0 && [self applyProfileAt:added.lastObject])
        [self showRoom:nil];
}

- (void)toggleRoom:(id)sender
{
    const bool on = !engine.roomOn.load();
    engine.roomOn = on;
    [prefs() setBool:!on forKey:@"roomOff"];
    [self updateLevelHold];
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
    return [self makeViewFor:engine.au size:NSMakeSize(1040, 660)];
}

- (NSView*)makeViewFor:(AudioUnit)unit size:(NSSize)want
{
    if (unit == nullptr)
        return nil;
    AudioUnitCocoaViewInfo info = {};
    UInt32 size = sizeof(info);
    if (AudioUnitGetProperty(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &info, &size) != noErr)
        return nil;

    NSURL* bundleURL = (__bridge_transfer NSURL*)info.mCocoaAUViewBundleLocation;
    NSString* className = (__bridge_transfer NSString*)info.mCocoaAUViewClass[0];
    NSBundle* bundle = [NSBundle bundleWithURL:bundleURL];
    Class cls = [bundle classNamed:className] ?: NSClassFromString(className);
    id<AUCocoaUIBase> factory = [cls new];
    if (factory == nil)
        return nil;
    return [factory uiViewForAudioUnit:unit withSize:want];
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
    if (roomWindow != nil && roomView != nil)
    {
        NSView* rv = [self makeViewFor:engine.room size:NSMakeSize(940, 540)];
        if (rv != nil)
            [self attachRoomView:rv];
    }
    if (window == nil || eqView == nil)
        return;
    NSView* view = [self makeView];
    if (view != nil)
        [self attachView:view];
}

// --- pump ROOM's window (its own view: load a profile, amount, phase)

- (void)attachRoomView:(NSView*)view
{
    if (roomFrameObserver != nil)
        [NSNotificationCenter.defaultCenter removeObserver:roomFrameObserver];
    roomFrameObserver = nil;
    const NSSize want = view.frame.size;
    roomView = view;
    roomWindow.contentView = view;
    [roomWindow setContentSize:want];
    [view setFrameSize:want];
    view.postsFrameChangedNotifications = YES;
    __weak AppDelegate* weakSelf = self;
    roomFrameObserver = [NSNotificationCenter.defaultCenter addObserverForName:NSViewFrameDidChangeNotification
                                                                        object:view
                                                                         queue:nil
                                                                    usingBlock:^(NSNotification*) {
        AppDelegate* me = weakSelf;
        if (me == nil || me->roomWindow == nil || me->roomView == nil)
            return;
        NSRect content = [me->roomWindow contentRectForFrameRect:me->roomWindow.frame];
        const NSSize size = me->roomView.frame.size;
        if (NSEqualSizes(content.size, size))
            return;
        NSRect frame = [me->roomWindow frameRectForContentRect:
            NSMakeRect(content.origin.x, NSMaxY(content) - size.height, size.width, size.height)];
        [me->roomWindow setFrame:frame display:YES];
    }];
    [roomWindow makeFirstResponder:keyViewIn(view) ?: view];
}

- (void)showRoom:(id)sender
{
    if (engine.room == nullptr)
        return;
    if (roomWindow == nil)
    {
        roomWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 940, 540)
                                                 styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                           NSWindowStyleMaskMiniaturizable
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
        roomWindow.releasedWhenClosed = NO;
        roomWindow.delegate = self;
        if (![roomWindow setFrameUsingName:@"SystemEQRoom"])
            [roomWindow center];
        [roomWindow setFrameAutosaveName:@"SystemEQRoom"];
    }
    if (roomView == nil)
    {
        NSView* view = [self makeViewFor:engine.room size:NSMakeSize(940, 540)];
        if (view == nil)
            return;
        const NSPoint topLeft = NSMakePoint(NSMinX(roomWindow.frame), NSMaxY(roomWindow.frame));
        [self attachRoomView:view];
        [roomWindow setFrameTopLeftPoint:topLeft];
    }
    [self updateStatus];
    [NSApp activateIgnoringOtherApps:YES];
    [roomWindow makeKeyAndOrderFront:nil];
    [roomWindow makeFirstResponder:keyViewIn(roomView) ?: roomView];
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
    if (note.object == roomWindow && roomWindow != nil)
    {
        if (roomFrameObserver != nil)
            [NSNotificationCenter.defaultCenter removeObserver:roomFrameObserver];
        roomFrameObserver = nil;
        roomWindow.contentView = [[NSView alloc] initWithFrame:NSZeroRect];
        roomView = nil;
        return;
    }
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

    // room correction (its profile included) alongside
    if (engine.room == nullptr)
        return;
    plist = nullptr;
    size = sizeof(plist);
    if (AudioUnitGetProperty(engine.room, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, &size)
        != noErr || plist == nullptr)
        return;
    NSData* rdata = [NSPropertyListSerialization dataWithPropertyList:(__bridge id)plist
                                                               format:NSPropertyListBinaryFormat_v1_0
                                                              options:0
                                                                error:nil];
    CFRelease(plist);
    [rdata writeToURL:roomStateURL(stateKey) atomically:YES];
    if (stateKey != nil)
        [rdata writeToURL:roomLastStateURL() atomically:YES];
}

- (BOOL)loadStateFor:(NSString*)key
{
    // room correction: this output's own, else whatever it had (a new output keeps the current profile)
    [self loadRoomStateFrom:roomStateURL(key)];
    return [self loadStateFrom:stateURL(key)];
}

- (BOOL)loadRoomStateFrom:(NSURL*)url
{
    if (engine.room == nullptr)
        return NO;
    NSData* data = [NSData dataWithContentsOfURL:url];
    if (data == nil)
        return NO;
    id plist = [NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:nil];
    if (plist == nil)
        return NO;
    CFPropertyListRef ref = (__bridge CFPropertyListRef)plist;
    return AudioUnitSetProperty(engine.room, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &ref,
                                sizeof(ref)) == noErr;
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
    [self updateLevelHold];
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
