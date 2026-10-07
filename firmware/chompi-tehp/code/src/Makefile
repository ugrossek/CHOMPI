# Project Name
TARGET = TEHP

# TEHP shares TAPE's prebuilt libraries instead of carrying a copy. Rebuilding
# them with a different compiler is what breaks the SD card, so use these.
CHOMPI_LIBS ?= ../../../chompi-tape/code/libs

DEBUG=0
# OPT=-O0
OPT=-O3

APP_TYPE=BOOT_SRAM
LDSCRIPT = chompi_sram.lds

LDSCRIPT=chompi_sram.lds

# FreeRTOS coreJSON
C_SOURCES = $(CHOMPI_LIBS)/coreJSON/source/core_json.c
C_INCLUDES +=  -I$(CHOMPI_LIBS)/coreJSON/source/include

# Sources
CPP_SOURCES = chompi_main.cpp \
	encoder.cpp \
	FileStreamingManager.cpp

USE_FATFS = 1

# Libs
LIBDAISY_DIR = $(CHOMPI_LIBS)/libDaisy
DAISYSP_DIR = $(CHOMPI_LIBS)/DaisySP

# test builds: `make CPU_METER=1` shows the audio load on the white keys
ifeq ($(CPU_METER),1)
C_DEFS += -DTEHP_CPU_METER
endif

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
