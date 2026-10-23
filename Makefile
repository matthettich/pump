#!/usr/bin/make -f
# …pump: build every plugin (VST3 + CLAP, plus AU on macOS) and run the DSP tests.
#   make            build all plugins into bin/
#   make eq         build one plugin
#   make tests      build and run the offline DSP tests
#   make jack       standalone apps (local testing)

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

tests: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

clean:
	rm -rf bin build

.PHONY: all dgl jack tests clean dpf-patches $(PLUGINS)
