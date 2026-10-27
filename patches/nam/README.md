# Patches to the NAM core

Fixes to the NAM core (`third_party/NeuralAmpModelerCore` submodule, used by AMP) that pump needs and upstream doesn't
have yet. `make` applies them automatically, in order (`tools/apply-nam-patches.sh`, idempotent; AMP's own Makefile
runs it too), so CI and local builds both get them; the submodule itself stays on upstream's commit. If a NAM update
makes one fail to apply, refresh or drop it.

| Patch | Fixes |
|---|---|
| 0001-bounds-checked-weights-iterator | A `.nam` file whose weight count didn't match its architecture made every `set_weights_` read past the end of the weights list (heap-buffer-overflow while the network was built: memory corruption inside the host, found by `tests/fuzz_nam.cpp`). The weights cursor is now `nam::weights_iterator`, which knows the end and throws on a read past it; the end-of-load checks use it, and the LSTM's (an `assert`, so never checked in release builds) now throws. Upstream's own tests under `tools/test/` still name `std::vector<float>::iterator` and would need the same rename; pump doesn't build them. |

Not a patch, but the other half of the same finding: a model with a config field missing hit an `assert` inside the
JSON library (an abort in debug builds, an out-of-range read in release). `plugins/amp/NamJson.hpp` turns those
asserts into exceptions for AMP's copy of NAM, and `ModelGuard.hpp` rejects architecture numbers that overflow an int.

Worth sending upstream: https://github.com/sdatkinson/NeuralAmpModelerCore
