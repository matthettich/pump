# Shared build setup for every pump plugin. A plugin's Makefile sets NAME, FILES_DSP and FILES_UI, then
# includes this file. Formats: VST3 and CLAP everywhere, AU on macOS; `make jack` builds a standalone app.

DPF_PATH ?= ../../dpf
DPF_TARGET_DIR ?= ../../bin
DPF_BUILD_DIR ?= ../../build/$(NAME)
UI_TYPE = opengl

# DPF fixes pump carries (patches/dpf): also applied when a plugin is built directly (make -C plugins/eq)
ifeq ($(shell sh ../../tools/apply-dpf-patches.sh $(DPF_PATH) >&2 && echo ok),)
$(error DPF patches could not be applied, see above)
endif

include $(DPF_PATH)/Makefile.plugins.mk

# C++17, our headers, and fast math without assuming finite values (meters use -inf as "silence").
BUILD_CXX_FLAGS += -std=gnu++17 -I. -I../../common -fno-finite-math-only -Wno-unused-parameter -DPFFFT_STATIC_DEFINE
BUILD_C_FLAGS += -fno-finite-math-only -Wno-unused-parameter -Wno-sign-compare -DPFFFT_STATIC_DEFINE

TARGETS = vst3 clap
ifeq ($(MACOS),true)
TARGETS += au
endif

all: $(TARGETS)
