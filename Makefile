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

# DPF's graphics library is shared by every plugin: build it once, first, so parallel plugin builds (make -j)
# don't race to write it.
dgl: dpf-patches
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
build/tests/nam.o: plugins/amp/nam_unity.cpp $(wildcard $(NAM_DIR)/NAM/* $(NAM_DIR)/NAM/*/*)
	@mkdir -p build/tests
	$(CXX) -O2 $(NAM_FLAGS) -c $< -o $@
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

tests: $(TESTS) $(SYNC_TESTS) build/tests/wasm_wrapper
	@for t in $(TESTS) $(SYNC_TESTS) build/tests/wasm_wrapper; do echo "== $$t"; ./$$t || exit 1; done

# pump.wasm + pump-meta.js for Thunder Plus (WASI_SDK = a wasi-sdk 25 folder; see wasm/build.sh)
wasm:
	sh wasm/build.sh

clean:
	rm -rf bin build

.PHONY: all dgl jack tests clean dpf-patches wasm $(PLUGINS)
