/* A stand-in libjack.so.0 for running pump's standalone (`make jack`) builds headless, for tools/shot.sh: no JACK
 * server, no sound card. It runs the plugin's process callback on a thread at the real rate and feeds it
 *   PUMP_TEST_AUDIO=file.f32   audio input, raw float32 interleaved stereo, looped (tools/make_mix.py makes one)
 *   PUMP_TEST_NOTES=steps      MIDI notes: steps separated by ',', each a note number, a chord ("60+64+67") or a rest
 *                              ("-"); PUMP_TEST_STEP=seconds per step (default 0.4), each note held 80 % of its step,
 *                              velocity PUMP_TEST_VEL (default 110); the sequence loops
 * Build: cc -shared -fPIC -O2 -o build/headless-jack/libjack.so.0 tools/headless_jack.c -lpthread
 * Use:   LD_LIBRARY_PATH=build/headless-jack bin/pump-strike   (tools/shot.sh does this when the library is built)
 * Only what DPF's JACK standalone calls is here; DPF loads the rest as missing and doesn't use it. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint32_t jack_nframes_t;
typedef int (*ProcessCb)(jack_nframes_t, void*);
typedef int (*NframesCb)(jack_nframes_t, void*);
typedef void (*ThreadInitCb)(void*);
typedef void (*ShutdownCb)(void*);
typedef struct { jack_nframes_t time; size_t size; unsigned char* buffer; } MidiEvent;

enum { kSR = 48000, kBlock = 256, kMaxPorts = 64, kMaxEv = 64 };
#define kPortIsInput 0x1

typedef struct Port { int midi, input, chan; float buf[8192]; MidiEvent ev[kMaxEv]; unsigned char data[kMaxEv][3]; int nev; } Port;
typedef struct Client {
    char name[64];
    ProcessCb process; void* processArg;
    ThreadInitCb threadInit; void* threadInitArg;
    Port* ports[kMaxPorts]; int nports, audioIns;
    pthread_t thread; volatile int running;
} Client;

static float* gAudio; static size_t gAudioFrames;
static int gSteps[256][8], gStepN[256], gNSteps; static double gStepSec = 0.4; static int gVel = 110;
static uint64_t gFrame;

static void loadEnv(void)
{
    const char* a = getenv("PUMP_TEST_AUDIO");
    if (a && *a)
    {
        FILE* f = fopen(a, "rb");
        if (f)
        {
            fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
            gAudio = (float*)malloc((size_t)n);
            if (gAudio && fread(gAudio, 1, (size_t)n, f) == (size_t)n) gAudioFrames = (size_t)n / 8; else gAudioFrames = 0;
            fclose(f);
        }
    }
    const char* s = getenv("PUMP_TEST_STEP"); if (s) gStepSec = atof(s); if (gStepSec < 0.02) gStepSec = 0.02;
    const char* v = getenv("PUMP_TEST_VEL"); if (v) gVel = atoi(v); if (gVel < 1 || gVel > 127) gVel = 110;
    const char* n = getenv("PUMP_TEST_NOTES");
    if (!n) return;
    char buf[2048]; strncpy(buf, n, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    char* save = NULL;
    for (char* step = strtok_r(buf, ",", &save); step && gNSteps < 256; step = strtok_r(NULL, ",", &save))
    {
        int k = 0; char* s2 = NULL;
        for (char* tok = strtok_r(step, "+", &s2); tok && k < 8; tok = strtok_r(NULL, "+", &s2))
            if (*tok != '-') gSteps[gNSteps][k++] = atoi(tok);
        gStepN[gNSteps++] = k;
    }
}

static void addEvent(Port* p, jack_nframes_t t, unsigned char st, unsigned char d1, unsigned char d2)
{
    if (p->nev >= kMaxEv) return;
    p->data[p->nev][0] = st; p->data[p->nev][1] = d1; p->data[p->nev][2] = d2;
    p->ev[p->nev].time = t; p->ev[p->nev].size = 3; p->ev[p->nev].buffer = p->data[p->nev];
    p->nev++;
}

/* this block's input: audio from the file, and the note-ons / note-offs that fall inside it */
static void fillInputs(Client* c)
{
    const double stepF = gStepSec * kSR, holdF = stepF * 0.8;
    for (int i = 0; i < c->nports; ++i)
    {
        Port* p = c->ports[i];
        if (!p->input) continue;
        if (!p->midi)
        {
            for (int k = 0; k < kBlock; ++k)
                p->buf[k] = gAudioFrames ? gAudio[((gFrame + (uint64_t)k) % gAudioFrames) * 2 + (p->chan & 1)] : 0.0f;
            continue;
        }
        p->nev = 0;
        if (!gNSteps) continue;
        for (int k = 0; k < kBlock; ++k)
        {
            const uint64_t f = gFrame + (uint64_t)k;
            const uint64_t step = (uint64_t)((double)f / stepF);
            const double into = (double)f - (double)step * stepF;
            const int s = (int)(step % (uint64_t)gNSteps);
            if (into < 1.0)
                for (int j = 0; j < gStepN[s]; ++j) addEvent(p, (jack_nframes_t)k, 0x90, (unsigned char)gSteps[s][j], (unsigned char)gVel);
            if (into >= holdF && into < holdF + 1.0)
                for (int j = 0; j < gStepN[s]; ++j) addEvent(p, (jack_nframes_t)k, 0x80, (unsigned char)gSteps[s][j], 0);
        }
    }
}

static void* run(void* arg)
{
    Client* c = (Client*)arg;
    if (c->threadInit) c->threadInit(c->threadInitArg);
    struct timespec next; clock_gettime(CLOCK_MONOTONIC, &next);
    const long blockNs = (long)(1e9 * kBlock / kSR);
    while (c->running)
    {
        fillInputs(c);
        if (c->process) c->process(kBlock, c->processArg);
        gFrame += kBlock;
        next.tv_nsec += blockNs;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    return NULL;
}

void* jack_client_open(const char* name, int options, int* status, ...)
{
    (void)options;
    loadEnv();
    Client* c = (Client*)calloc(1, sizeof(Client));
    snprintf(c->name, sizeof c->name, "%s", name ? name : "pump");
    if (status) *status = 0;
    return c;
}
int jack_client_close(void* cl) { Client* c = (Client*)cl; if (c && c->running) { c->running = 0; pthread_join(c->thread, NULL); } return 0; }
char* jack_get_client_name(void* cl) { return ((Client*)cl)->name; }
int jack_activate(void* cl) { Client* c = (Client*)cl; c->running = 1; return pthread_create(&c->thread, NULL, run, c); }
int jack_deactivate(void* cl) { Client* c = (Client*)cl; if (c->running) { c->running = 0; pthread_join(c->thread, NULL); } return 0; }
jack_nframes_t jack_get_buffer_size(void* cl) { (void)cl; return kBlock; }
jack_nframes_t jack_get_sample_rate(void* cl) { (void)cl; return kSR; }
int jack_set_process_callback(void* cl, ProcessCb cb, void* arg) { Client* c = (Client*)cl; c->process = cb; c->processArg = arg; return 0; }
int jack_set_buffer_size_callback(void* cl, NframesCb cb, void* arg) { (void)cl; (void)cb; (void)arg; return 0; }
int jack_set_sample_rate_callback(void* cl, NframesCb cb, void* arg) { (void)cl; (void)cb; (void)arg; return 0; }
int jack_set_thread_init_callback(void* cl, ThreadInitCb cb, void* arg) { Client* c = (Client*)cl; c->threadInit = cb; c->threadInitArg = arg; return 0; }
void jack_on_shutdown(void* cl, ShutdownCb cb, void* arg) { (void)cl; (void)cb; (void)arg; }
void* jack_port_register(void* cl, const char* name, const char* type, unsigned long flags, unsigned long bufsize)
{
    (void)name; (void)bufsize;
    Client* c = (Client*)cl;
    if (c->nports >= kMaxPorts) return NULL;
    Port* p = (Port*)calloc(1, sizeof(Port));
    p->midi = type && strstr(type, "midi") != NULL;
    p->input = (flags & kPortIsInput) != 0;
    if (p->input && !p->midi) p->chan = c->audioIns++;
    c->ports[c->nports++] = p;
    return p;
}
int jack_port_unregister(void* cl, void* port) { (void)cl; (void)port; return 0; }
void* jack_port_get_buffer(void* port, jack_nframes_t n) { (void)n; Port* p = (Port*)port; return p->midi ? (void*)p : (void*)p->buf; }
uint64_t jack_port_uuid(const void* port) { (void)port; return 0; }
uint32_t jack_midi_get_event_count(void* buf) { Port* p = (Port*)buf; return p->input ? (uint32_t)p->nev : 0; }
int jack_midi_event_get(MidiEvent* ev, void* buf, uint32_t i)
{
    Port* p = (Port*)buf;
    if (!p->input || i >= (uint32_t)p->nev) return -1;
    *ev = p->ev[i];
    return 0;
}
void jack_midi_clear_buffer(void* buf) { (void)buf; }
int jack_midi_event_write(void* buf, jack_nframes_t t, const unsigned char* d, size_t n) { (void)buf; (void)t; (void)d; (void)n; return 0; }
int jack_transport_query(const void* cl, void* pos) { (void)cl; if (pos) memset(pos, 0, 136); return 0; }
int jack_set_property(void* cl, uint64_t subject, const char* key, const char* value, const char* type) { (void)cl; (void)subject; (void)key; (void)value; (void)type; return 0; }
