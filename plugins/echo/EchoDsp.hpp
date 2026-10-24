// pump ECHO: the echoes' pitch shifter (shared: fx::PitchShifter in common/dsp/Fx.hpp).
#pragma once

#include "dsp/Fx.hpp"

namespace pump { namespace echo {

using AlignedPitchShifter = fx::PitchShifter;   // (now shared: common/dsp/Fx.hpp)

}} // namespace pump::echo
