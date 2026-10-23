// The shared test mix (tests/data/mix.f32: 8 bars at 48 kHz, raw float32 interleaved stereo, made by
// tools/make_mix.py). `make tests` runs from the repo root; set PUMP_TEST_MIX to run a test from elsewhere.
#pragma once
#include <cstdlib>

inline const char* pump_test_mix()
{
    const char* e = std::getenv("PUMP_TEST_MIX");
    return (e && *e) ? e : "tests/data/mix.f32";
}
