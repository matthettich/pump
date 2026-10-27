#!/usr/bin/make -f
# …pump: build every plugin (mastering + effects; VST3 + CLAP, plus AU on macOS) and run the DSP tests.
#   make            build all plugins into bin/
#   make eq         build one plugin
#   make tests      build and run the offline DSP tests
#   make jack       standalone apps (local testing)
#   make wasm       pump.wasm + pump-meta.js for Thunder Plus (WASI_SDK=...; wasm/build.sh)

PLUGINS := $(patsubst plugins/%/Makefile,%,$(wildcard plugins/*/Makefile))

all: $(PLUGINS)

# fixes to DPF that pump carries as patches (patches/dpf/README.md); applied once, before any plugin builds
dpf-patches:
	@sh tools/apply-dpf-patches.sh

# fixes to the NAM core (AMP), the same way: patches/nam/
nam-patches:
	@sh tools/apply-nam-patches.sh

# DPF's graphics library is shared by every plugin: build it once, first, so parallel plugin builds (make -j)
# don't race to write it.
dgl: dpf-patches nam-patches
	$(MAKE) -C dpf/dgl opengl

$(PLUGINS): dgl
	$(MAKE) -C plugins/$@

jack: dgl
	for p in $(PLUGINS); do $(MAKE) -C plugins/$$p jack || exit 1; done

# --- offline DSP tests (no UI, no plugin framework) ---
CXX ?= c++
CC ?= cc
TEST_FLAGS = -O2 -std=c++17 -Icommon -Wall -Wno-unused-parameter
TESTS := $(patsubst tests/%.cpp,build/tests/%,$(wildcard tests/test_*.cpp))

build/tests/pffft.o: common/pump_pffft.c
	@mkdir -p build/tests
	$(CC) -O2 -c $< -o $@

# a test also depends on its plugin's headers (tests/test_tone.cpp -> plugins/tone/*.hpp)
.SECONDEXPANSION:
build/tests/%: tests/%.cpp build/tests/pffft.o $(wildcard common/*.hpp common/dsp/*.hpp tests/*.hpp) $$(wildcard plugins/$$(word 2,$$(subst _, ,$$*))/*.hpp)
	@mkdir -p build/tests
	$(CXX) $(TEST_FLAGS) -Iplugins/$(word 2,$(subst _, ,$*)) $< build/tests/pffft.o -lpthread -o $@

# the preset check reads every plugin's tables
build/tests/test_presets: $(wildcard plugins/*/*.hpp)

# The kernel-building engines again, built as for WebAssembly (PUMP_SYNC_KERNELS: no worker thread, see
# common/dsp/Util.hpp), and the wasm wrapper itself (wasm/pump_wasm.cpp, natively: the effects through its C interface).
SYNC_TESTS := $(patsubst %,build/tests/sync_test_%,eq band wide heat)
build/tests/sync_test_%: tests/test_%.cpp build/tests/pffft.o $(wildcard common/*.hpp common/dsp/*.hpp tests/*.hpp) $$(wildcard plugins/$$*/*.hpp)
	@mkdir -p build/tests
	$(CXX) $(TEST_FLAGS) -DPUMP_SYNC_KERNELS=1 -Iplugins/$* $< build/tests/pffft.o -lpthread -o $@
build/tests/wasm_wrapper: tests/wasm_wrapper.cpp wasm/pump_wasm.cpp build/tests/pffft.o $(wildcard common/*.hpp common/dsp/*.hpp plugins/*/*.hpp)
	@mkdir -p build/tests
	$(CXX) $(TEST_FLAGS) -DPUMP_SYNC_KERNELS=1 $< build/tests/pffft.o -o $@

# AMP's test links the NAM core (float samples, Eigen from the NAM submodule; EIGEN_PATH= to use another copy)
NAM_DIR := third_party/NeuralAmpModelerCore
EIGEN_PATH ?= $(NAM_DIR)/Dependencies/eigen
NAM_FLAGS = -DNAM_SAMPLE_FLOAT -I$(NAM_DIR) -I$(EIGEN_PATH) -I$(NAM_DIR)/Dependencies/nlohmann
build/tests/nam.o: plugins/amp/nam_unity.cpp $(wildcard $(NAM_DIR)/NAM/* $(NAM_DIR)/NAM/*/*) $(wildcard patches/nam/*.patch) | nam-patches
	@mkdir -p build/tests
	$(CXX) -O2 -std=c++17 $(NAM_FLAGS) -c $< -o $@
# AMP's model pre-check test reads the NAM example models and needs only the JSON header, not NAM's code
build/tests/test_modelguard: tests/test_modelguard.cpp plugins/amp/ModelGuard.hpp
	@mkdir -p build/tests
	$(CXX) $(TEST_FLAGS) -I$(NAM_DIR)/Dependencies/nlohmann -Iplugins/amp $< -o $@
build/tests/test_amp: tests/test_amp.cpp build/tests/pffft.o build/tests/nam.o $(wildcard common/*.hpp common/dsp/*.hpp tests/*.hpp plugins/amp/*.hpp)
	@mkdir -p build/tests
	$(CXX) $(TEST_FLAGS) $(NAM_FLAGS) -Iplugins/amp $< build/tests/pffft.o build/tests/nam.o -lpthread -o $@

# MACHINE's test links the drum machine firmware (C, built the way the plugin builds it: plugins/machine/Makefile)
MFW := plugins/machine/fw
MFW_FLAGS = -O2 -std=gnu11 -funsigned-char -w -I$(MFW)/stub -I$(MFW)/firmware/Core/Inc -I$(MFW) $(if $(shell $(CC) --version 2>/dev/null | grep -i clang),,-D__fp16=_Float16)
build/tests/mfw_%.o: $(MFW)/machine_%.c $(MFW)/machine_fw.c $(MFW)/machine_fw.h
	@mkdir -p build/tests
	$(CC) $(MFW_FLAGS) -c $< -o $@
build/tests/test_machine: tests/test_machine.cpp build/tests/pffft.o build/tests/mfw_unity.o build/tests/mfw_unity2.o $(wildcard common/*.hpp common/dsp/*.hpp tests/*.hpp plugins/machine/*.hpp)
	@mkdir -p build/tests
	$(CXX) $(TEST_FLAGS) -Iplugins/machine $< build/tests/pffft.o build/tests/mfw_unity.o build/tests/mfw_unity2.o -lpthread -o $@

# MACHINE (GPL-3.0) must not leak into any other plugin or into common/ (see LICENSE)
licence-check:
	@sh tools/check-licence-boundary.sh

tests: licence-check $(TESTS) $(SYNC_TESTS) build/tests/wasm_wrapper
	@for t in $(TESTS) $(SYNC_TESTS) build/tests/wasm_wrapper; do echo "== $$t"; ./$$t || exit 1; done

# --- sanitizer runs: the same DSP tests built with ASan+UBSan, or with TSan (the engines hand data to the UI through
# atomic feeds and to a worker thread, which TSan checks). Sanitized builds go to build/asan, build/tsan, and the
# tests that need submodules or the C firmware (amp, machine) are left to the normal run. Every other test runs:
#   make tests-asan      memory errors and undefined behaviour
#   make tests-tsan      data races
# (SAN_TESTS names the quick, self-contained ones; add to it as more are checked.)
SAN_TESTS ?= wav biquad dsp presets table eq band wide heat gate tune sing_bank boing
ASAN_FLAGS = -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined
TSAN_FLAGS = -O1 -g -fno-omit-frame-pointer -fsanitize=thread

build/%/pffft.o: common/pump_pffft.c
	@mkdir -p build/$*
	$(CC) -O1 -g -c $< -o $@

define SAN_RULE
build/$(1)/test_%: tests/test_%.cpp build/$(1)/pffft.o $$(wildcard common/*.hpp common/dsp/*.hpp tests/*.hpp) $$$$(wildcard plugins/$$$$*/*.hpp)
	@mkdir -p build/$(1)
	$$(CXX) $$(TEST_FLAGS) $(2) -Iplugins/$$* $$< build/$(1)/pffft.o -lpthread -o $$@
endef
$(eval $(call SAN_RULE,asan,$(ASAN_FLAGS)))
$(eval $(call SAN_RULE,tsan,$(TSAN_FLAGS)))

tests-asan: $(patsubst %,build/asan/test_%,$(SAN_TESTS))
	@mkdir -p build/tests   # (test_wav writes its files there)
	@for t in $(SAN_TESTS); do echo "== asan $$t"; ./build/asan/test_$$t || exit 1; done
tests-tsan: $(patsubst %,build/tsan/test_%,$(SAN_TESTS))
	@mkdir -p build/tests   # (test_wav writes its files there)
	@for t in $(SAN_TESTS); do echo "== tsan $$t"; ./build/tsan/test_$$t || exit 1; done

# pump.wasm + pump-meta.js for Thunder Plus (WASI_SDK = a wasi-sdk 25 folder; see wasm/build.sh)
wasm:
	sh wasm/build.sh

clean:
	rm -rf bin build

.PHONY: all dgl jack tests clean dpf-patches nam-patches wasm $(PLUGINS)
