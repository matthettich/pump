// System EQ: pump EQ on everything the Mac plays.
//
// A menu-bar app (macOS 14.2+). It taps the mix of every other app with a Core Audio process tap (muted while it is
// being read, so the original sound stops), runs it through the pump EQ Audio Unit bundled inside the app
// (Contents/PlugIns/pump-eq.component, the same build as the plugin, with its own window), and plays the result on the
// current output device. Turning it off destroys the tap, so the Mac goes back to its untouched output.
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
#include <atomic>
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

    // audio-thread buffers (allocated once)
    std::vector<float> inL, inR, outL, outR;
    Float64 sampleTime = 0.0;
    std::atomic<bool> running { false };

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

        // silence every output channel first; only the stereo pair is written below
        for (UInt32 b = 0; b < outOutput->mNumberBuffers; ++b)
            if (outOutput->mBuffers[b].mData != nullptr)
                std::memset(outOutput->mBuffers[b].mData, 0, outOutput->mBuffers[b].mDataByteSize);

        UInt32 total = framesIn(outOutput);
        if (total == 0 || e->au == nullptr)
            return noErr;

        // the tap arrives as one stereo mixdown (interleaved or not: readSlice handles both)
        const UInt32 inFrames = framesIn(inInput);

        UInt32 done = 0;
        while (done < total)
        {
            const UInt32 n = (total - done) < kMaxFrames ? (total - done) : kMaxFrames;

            if (inFrames >= done + n)
            {
                // read the slice [done, done+n) of the tap
                readSlice(inInput, 0, e->inL.data(), done, n);
                readSlice(inInput, 1, e->inR.data(), done, n);
            }
            else
            {
                std::memset(e->inL.data(), 0, n * sizeof(float));
                std::memset(e->inR.data(), 0, n * sizeof(float));
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
            }
            e->sampleTime += n;

            writeChannel(outOutput, e->outLeft, static_cast<float*>(out.list.mBuffers[0].mData), n, done);
            writeChannel(outOutput, e->outRight, static_cast<float*>(out.second.mData), n, done);
            done += n;
        }
        return noErr;
    }

    static void readSlice(const AudioBufferList* list, UInt32 ch, float* dst, UInt32 offset, UInt32 frames)
    {
        UInt32 first = 0;
        for (UInt32 b = 0; b < list->mNumberBuffers; ++b)
        {
            const AudioBuffer& buf = list->mBuffers[b];
            const UInt32 nc = buf.mNumberChannels;
            if (ch < first + nc && buf.mData != nullptr && nc > 0)
            {
                const float* src = static_cast<const float*>(buf.mData) + (ch - first) + offset * nc;
                for (UInt32 i = 0; i < frames; ++i)
                    dst[i] = src[i * nc];
                return;
            }
            first += nc;
        }
        // a mono tap: duplicate channel 0
        if (ch == 1 && list->mNumberBuffers > 0 && list->mBuffers[0].mNumberChannels == 1)
            readSlice(list, 0, dst, offset, frames);
        else
            std::memset(dst, 0, frames * sizeof(float));
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

        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
        AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, sizeof(fmt));

        UInt32 maxFrames = kMaxFrames;
        AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
                             &maxFrames, sizeof(maxFrames));

        AURenderCallbackStruct cb = { renderInput, this };
        AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));

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

        // every process except this one, mixed to stereo, muted while we read it
        NSMutableArray<NSNumber*>* exclude = [NSMutableArray array];
        const AudioObjectID self = ownProcessObject();
        if (self != kAudioObjectUnknown)
            [exclude addObject:@(self)];

        CATapDescription* desc = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:exclude];
        desc.name = kAppName;
        desc.privateTap = YES;
        desc.muteBehavior = CATapMutedWhenTapped;

        OSStatus err = AudioHardwareCreateProcessTap(desc, &tap);
        if (err != noErr || tap == kAudioObjectUnknown)
        {
            tap = kAudioObjectUnknown;
            return [NSString stringWithFormat:@"Couldn't tap system audio (error %d).", (int)err];
        }

        NSString* tapUID = desc.UUID.UUIDString;

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
            AudioObjectSetPropertyData(aggregate, &a, 0, nullptr, sizeof(aggRate), &aggRate);
            getProp(aggregate, kAudioDevicePropertyNominalSampleRate, aggRate);
        }

        // keep IO slices within what the EQ was told to expect
        {
            UInt32 frames = 512;
            AudioObjectPropertyAddress a = addr(kAudioDevicePropertyBufferFrameSize);
            AudioObjectSetPropertyData(aggregate, &a, 0, nullptr, sizeof(frames), &frames);
        }

        if (!prepareUnit(aggRate))
        {
            stop();
            return @"The EQ could not start.";
        }

        err = AudioDeviceCreateIOProcID(aggregate, ioProcFn, this, &ioProc);
        if (err == noErr)
            err = AudioDeviceStart(aggregate, ioProc);
        if (err != noErr)
        {
            stop();
            return [NSString stringWithFormat:@"Couldn't start audio (error %d).", (int)err];
        }

        running = true;
        return nil;
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
    }
};

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

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, NSMenuDelegate>
@end

@implementation AppDelegate
{
    Engine engine;
    NSStatusItem* statusItem;
    NSWindow* window;
    NSView* eqView;
    NSString* problem;
    BOOL wantOn;
    NSTimer* saveTimer;
}

static NSURL* stateURL()
{
    NSURL* dir = [[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory
                                                       inDomains:NSUserDomainMask].firstObject
                  URLByAppendingPathComponent:@"wake/System EQ" isDirectory:YES];
    [NSFileManager.defaultManager createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:nil];
    return [dir URLByAppendingPathComponent:@"eq-state.plist"];
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
        engine.prepareUnit(48000.0);
        [self restoreState];
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

    wantOn = ![NSUserDefaults.standardUserDefaults boolForKey:@"off"];
    if (wantOn)
        [self startEngine];

    saveTimer = [NSTimer scheduledTimerWithTimeInterval:30 repeats:YES block:^(NSTimer*) { [self saveState]; }];

    if (![NSUserDefaults.standardUserDefaults boolForKey:@"launchedBefore"])
    {
        [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"launchedBefore"];
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
    problem = engine.start();
    if (problem != nil)
        NSLog(@"%@: %@", kAppName, problem);
    [self updateStatus];
}

- (void)toggle:(id)sender
{
    wantOn = !wantOn;
    [NSUserDefaults.standardUserDefaults setBool:!wantOn forKey:@"off"];
    if (wantOn)
        [self startEngine];
    else
    {
        engine.stop();
        problem = nil;
        [self updateStatus];
    }
}

- (void)updateStatus
{
    statusItem.button.appearsDisabled = !engine.running;
    window.title = engine.running ? @"System EQ — on" : @"System EQ — off";
}

// restart on a new default output, a sample-rate change or a device going away
static OSStatus deviceListener(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void* ctx)
{
    AppDelegate* app = (__bridge AppDelegate*)ctx;
    dispatch_async(dispatch_get_main_queue(), ^{ [app devicesChanged]; });
    return noErr;
}

- (void)listenForDeviceChanges
{
    AudioObjectPropertyAddress a = addr(kAudioHardwarePropertyDefaultOutputDevice);
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &a, deviceListener, (__bridge void*)self);
    AudioObjectPropertyAddress d = addr(kAudioHardwarePropertyDevices);
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &d, deviceListener, (__bridge void*)self);
}

- (void)devicesChanged
{
    if (!wantOn)
        return;
    // our own aggregate appearing/disappearing also changes the device list: only restart for real changes
    Float64 rate = 0.0;
    const AudioObjectID out = defaultOutputDevice();
    getProp(out, kAudioDevicePropertyNominalSampleRate, rate);
    if (engine.running && out == engine.output && rate == engine.auRate)
        return;
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(startEngine) object:nil];
    [self performSelector:@selector(startEngine) withObject:nil afterDelay:0.3];
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
        ? [NSString stringWithFormat:@"Output: %@ · %.1f kHz", outName, engine.auRate / 1000.0]
        : [NSString stringWithFormat:@"Output: %@", outName];
    [menu addItemWithTitle:info action:nil keyEquivalent:@""].enabled = NO;
    if (problem != nil)
        [menu addItemWithTitle:problem action:nil keyEquivalent:@""].enabled = NO;

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
        @"x-apple.systempreferences:com.apple.preference.security?Privacy_AudioCapture"]];
}

// --- the EQ window (the plugin's own view)

- (void)showEQ:(id)sender
{
    if (engine.au == nullptr)
        return;

    if (window == nil)
    {
        AudioUnitCocoaViewInfo info = {};
        UInt32 size = sizeof(info);
        if (AudioUnitGetProperty(engine.au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &info, &size)
            != noErr)
            return;

        NSURL* bundleURL = (__bridge_transfer NSURL*)info.mCocoaAUViewBundleLocation;
        NSString* className = (__bridge_transfer NSString*)info.mCocoaAUViewClass[0];
        NSBundle* bundle = [NSBundle bundleWithURL:bundleURL];
        Class cls = [bundle classNamed:className] ?: NSClassFromString(className);
        id<AUCocoaUIBase> factory = [cls new];
        if (factory == nil)
            return;

        eqView = [factory uiViewForAudioUnit:engine.au withSize:NSMakeSize(1040, 660)];
        if (eqView == nil)
            return;

        window = [[NSWindow alloc] initWithContentRect:eqView.frame
                                             styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                       NSWindowStyleMaskMiniaturizable
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        window.releasedWhenClosed = NO;
        window.delegate = self;
        window.contentView = eqView;
        [window setFrameAutosaveName:@"SystemEQ"];
        if (![window setFrameUsingName:@"SystemEQ"])
            [window center];

        // the plugin resizes its own view (corner drag, scale): follow it
        eqView.postsFrameChangedNotifications = YES;
        [NSNotificationCenter.defaultCenter addObserverForName:NSViewFrameDidChangeNotification
                                                        object:eqView
                                                         queue:nil
                                                    usingBlock:^(NSNotification*) {
            NSRect content = [self->window contentRectForFrameRect:self->window.frame];
            if (NSEqualSizes(content.size, self->eqView.frame.size))
                return;
            NSRect frame = [self->window frameRectForContentRect:
                NSMakeRect(content.origin.x, NSMaxY(content) - self->eqView.frame.size.height,
                           self->eqView.frame.size.width, self->eqView.frame.size.height)];
            [self->window setFrame:frame display:YES];
        }];
        [self updateStatus];
    }

    [NSApp activateIgnoringOtherApps:YES];
    [window makeKeyAndOrderFront:nil];
    [window makeFirstResponder:eqView];
}

- (void)windowWillClose:(NSNotification*)note
{
    [self saveState];
}

// --- state: the EQ's full state, kept between launches

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
    [data writeToURL:stateURL() atomically:YES];
}

- (void)restoreState
{
    NSData* data = [NSData dataWithContentsOfURL:stateURL()];
    if (data == nil)
        return;
    id plist = [NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:nil];
    if (plist == nil)
        return;
    CFPropertyListRef ref = (__bridge CFPropertyListRef)plist;
    AudioUnitSetProperty(engine.au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &ref, sizeof(ref));
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
