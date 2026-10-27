// pump AMP: the JSON library NAM's core uses, with its assertions turned into exceptions. NAM reads a model's config
// with nlohmann's operator[], whose "the key exists" check is only an assert(): a .nam file with a field missing
// aborted the host in a debug build and read out of range in a release build. Here it throws, and AMP's loader turns
// that into "couldn't load". Include this instead of json.hpp everywhere in AMP (and first in nam_unity.cpp), so every
// file sees the same definition.
#pragma once
#include <stdexcept>
#if defined(JSON_ASSERT)
#error "NamJson.hpp must come before anything that includes json.hpp"
#endif
// (a function, not a throw in the macro: a few of the library's checks sit in noexcept functions, where a failure still
// ends the program, as the assert did; everywhere else it is now an exception)
[[noreturn]] inline void pumpNamJsonFailed() { throw std::runtime_error("damaged model (a field NAM needs is missing or the wrong type)"); }
#define JSON_ASSERT(x) ((x) ? (void)0 : ::pumpNamJsonFailed())
#include "json.hpp"
