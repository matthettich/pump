#!/usr/bin/make -f
NAME = pump-amp
FILES_DSP = PluginAmp.cpp pffft.c nam_unity.cpp
FILES_UI = UIAmp.cpp fonts.cpp
include ../Plugin.mk

# The NAM core (MIT) needs Eigen (the NAM submodule's own copy; EIGEN_PATH= to use another).
NAM_DIR = ../../third_party/NeuralAmpModelerCore
EIGEN_PATH ?= $(NAM_DIR)/Dependencies/eigen
BUILD_CXX_FLAGS += -DNAM_SAMPLE_FLOAT -I$(NAM_DIR) -isystem $(EIGEN_PATH) -I$(NAM_DIR)/Dependencies/nlohmann
