// A stand-in for DPF's DistrhoPlugin.hpp, for building a plugin's own Plugin<Name>.hpp into pump.wasm without DPF.
//
// Used by wasm/fx/<name>.cpp (one translation unit per plugin, see wasm/fx/Runner.hpp): with -Iwasm/shim before
// -Icommon, `#include "PumpPlugin.hpp"` brings this file in place of DPF's, and the plugin header compiles unchanged:
// the same sync(), run(), engines and bypass the plugin runs in a host. Only what pump's plugins use is here: the
// Plugin base (parameters, states, latency, the sample rate, the transport), Parameter / State / AudioPort / PortGroup,
// MidiEvent, TimePosition and String. A host calls the public virtuals below directly (a real DPF keeps them
// protected and calls them from its wrappers; in a plugin they stay `protected: … override`, which is fine).
//
// START_NAMESPACE_DISTRHO opens PUMP_SHIM_NS (each translation unit names its own: the plugins all say
// `using namespace pump::<name>;` inside it, so two of them can never share one namespace).
#pragma once

#include "DistrhoPluginInfo.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <mutex>

// wasi-sdk's single-threaded libc++ has no std::mutex; an AudioWorklet has one thread, so a lock is nothing to do
// (STUTTER guards its pattern text with one between the UI and the audio thread).
#if defined(_LIBCPP_HAS_NO_THREADS)
namespace std {
struct mutex { void lock() {} void unlock() {} bool try_lock() { return true; } };
template <class M> struct unique_lock {
    explicit unique_lock(M&) {} unique_lock(M&, try_to_lock_t) {}
    bool owns_lock() const { return true; } explicit operator bool() const { return true; }
};
}
#endif

#ifndef PUMP_SHIM_NS
 #error "define PUMP_SHIM_NS (a namespace of this translation unit's own)"
#endif
#define START_NAMESPACE_DISTRHO namespace PUMP_SHIM_NS {
#define END_NAMESPACE_DISTRHO }
#define USE_NAMESPACE_DISTRHO using namespace PUMP_SHIM_NS;
#define DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(C)
#define DISTRHO_DECLARE_NON_COPYABLE(C)
#ifndef DISTRHO_PLUGIN_WANT_TIMEPOS
 #define DISTRHO_PLUGIN_WANT_TIMEPOS 0
#endif
#ifndef DISTRHO_PLUGIN_WANT_MIDI_INPUT
 #define DISTRHO_PLUGIN_WANT_MIDI_INPUT 0
#endif
#ifndef DISTRHO_PLUGIN_WANT_PROGRAMS
 #define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#endif

START_NAMESPACE_DISTRHO

constexpr uint32_t d_version(uint32_t a, uint32_t b, uint32_t c) { return (a << 16) | (b << 8) | c; }

class String {
public:
    String() {}
    String(const char* s) : s_(s ? s : "") {}
    explicit String(int v) : s_(std::to_string(v)) {}
    String& operator=(const char* s) { s_ = s ? s : ""; return *this; }
    const char* buffer() const { return s_.c_str(); }
    const char* c_str() const { return s_.c_str(); }
    operator const char*() const { return s_.c_str(); }
    size_t length() const { return s_.size(); }
    bool isEmpty() const { return s_.empty(); }
    bool isNotEmpty() const { return !s_.empty(); }
private:
    std::string s_;
};

enum : uint32_t {
    kParameterIsAutomatable = 0x01, kParameterIsBoolean = 0x02, kParameterIsInteger = 0x04, kParameterIsLogarithmic = 0x08,
    kParameterIsOutput = 0x10, kParameterIsTrigger = 0x20, kParameterIsHidden = 0x40,
};
enum ParameterDesignation { kParameterDesignationNull = 0, kParameterDesignationBypass = 1 };
enum : uint32_t { kStateIsOnlyForUI = 0x20, kStateIsBase64Blob = 0x40, kStateIsFilenamePath = 0x02 };
enum : uint32_t { kAudioPortIsCV = 0x1, kAudioPortIsSidechain = 0x2 };
enum : uint32_t { kPortGroupNone = (uint32_t)-1, kPortGroupMono = (uint32_t)-2, kPortGroupStereo = (uint32_t)-3 };

struct ParameterRanges { float def = 0.0f, min = 0.0f, max = 1.0f; };
struct ParameterEnumerationValue { float value = 0.0f; String label; };
struct ParameterEnumerationValues {
    uint8_t count = 0; bool restrictedMode = false; ParameterEnumerationValue* values = nullptr;
    ~ParameterEnumerationValues() { delete[] values; }
};
struct Parameter {
    uint32_t hints = 0; String name, shortName, symbol, unit, description; ParameterRanges ranges;
    ParameterEnumerationValues enumValues; ParameterDesignation designation = kParameterDesignationNull; uint32_t groupId = 0;
    void initDesignation(ParameterDesignation d) { designation = d; }
};
struct State { uint32_t hints = 0; String key, defaultValue, label, description; };
struct AudioPort { uint32_t hints = 0; String name, symbol; uint32_t groupId = kPortGroupNone; };
struct PortGroup { String name, symbol; };

struct MidiEvent {
    static constexpr uint32_t kDataSize = 4;
    uint32_t frame = 0, size = 0;
    uint8_t data[kDataSize] = {};
    const uint8_t* dataExt = nullptr;
};

struct TimePosition {
    bool playing = false;
    uint64_t frame = 0;
    struct BarBeatTick {
        bool valid = false;
        int32_t bar = 1, beat = 1;
        double tick = 0.0, barStartTick = 0.0;
        float beatsPerBar = 4.0f, beatType = 4.0f;
        double ticksPerBeat = 1920.0, beatsPerMinute = 120.0;
    } bbt;
};

// The sample rate a plugin sees from its constructor on (DPF hands it over the same way, before construction).
inline double& shimRate() { static double r = 48000.0; return r; }

class Plugin {
public:
    Plugin(uint32_t, uint32_t, uint32_t) : fSampleRate(shimRate()) {}
    virtual ~Plugin() {}

    double getSampleRate() const { return fSampleRate; }
    uint32_t getBufferSize() const { return 4096; }
    const TimePosition& getTimePosition() const { return fTime; }
    bool isDummyInstance() const { return false; }
    void setLatency(uint32_t l) { fLatency = (int)l; }
    // for the host (wasm/fx/Runner.hpp)
    int shimLatency() const { return fLatency; }
    TimePosition& shimTime() { return fTime; }

    virtual const char* getLabel() const { return ""; }
    virtual const char* getDescription() const { return ""; }
    virtual const char* getMaker() const { return ""; }
    virtual const char* getHomePage() const { return ""; }
    virtual const char* getLicense() const { return ""; }
    virtual uint32_t getVersion() const { return 0; }
    virtual int64_t getUniqueId() const { return 0; }
    virtual void initAudioPort(bool, uint32_t, AudioPort&) {}
    virtual void initPortGroup(uint32_t, PortGroup&) {}
    virtual void ioChanged(uint16_t, uint16_t) {}
    virtual void initParameter(uint32_t, Parameter&) {}
    virtual float getParameterValue(uint32_t) const { return 0.0f; }
    virtual void setParameterValue(uint32_t, float) {}
    virtual void initProgramName(uint32_t, String&) {}
    virtual void loadProgram(uint32_t) {}
    virtual void initState(uint32_t, State&) {}
    virtual void setState(const char*, const char*) {}
    virtual String getState(const char*) const { return String(); }
    virtual void activate() {}
    virtual void deactivate() {}
    virtual void run(const float**, float**, uint32_t) {}
    virtual void run(const float** in, float** out, uint32_t n, const MidiEvent*, uint32_t) { run(in, out, n); }

private:
    double fSampleRate;
    TimePosition fTime;
    int fLatency = 0;
};

END_NAMESPACE_DISTRHO
