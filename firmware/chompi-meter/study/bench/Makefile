# CIRCLES sound study: the CPU benchmark (a test firmware, not an instrument).
#   make            build build/BENCH.bin (GCC Arm 10.3-2021.10 on the PATH)

TARGET = BENCH

CHOMPI_LIBS ?= ../../../chompi-tape/code/libs

DEBUG = 0
OPT = -O3
CPP_STANDARD = -std=gnu++17

APP_TYPE = BOOT_SRAM
LDSCRIPT = ../../src/chompi_sram.lds

CPP_SOURCES = bench_main.cpp \
	../../src/board/encoder.cpp

C_INCLUDES += -I../../src/board -I../../src

LIBDAISY_DIR = $(CHOMPI_LIBS)/libDaisy
DAISYSP_DIR = $(CHOMPI_LIBS)/DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
