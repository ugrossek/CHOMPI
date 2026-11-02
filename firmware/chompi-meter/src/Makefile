# meter firmware for the CHOMPI.
#   make            build build/CIRCLES.bin (GCC Arm 10.3-2021.10 on the PATH), the release
#   make SLIDE=1    ... with sliding a figure (loop held + wheel), for beta 2
#   make TEST=1 BUILD_DIR=build-test
#                   build build-test/CIRCLES-TEST.bin, with what is still being
#                   tried: the slide, a figure's versions (CHOMPI + loop held +
#                   wheel) and choosing its length after playing (loop held + a
#                   black key)
#   make DRIFT=1 BUILD_DIR=build-drift
#                   build build-drift/CIRCLES-DRIFT.bin: the slide, and every
#                   player at its own speed, pulled towards its place against L0

TARGET = CIRCLES
ifeq ($(SLIDE),1)
C_DEFS += -DCIRCLES_SLIDE=1
endif
ifeq ($(DRIFT),1)
TARGET = CIRCLES-DRIFT
C_DEFS += -DCIRCLES_DRIFT=1 -DCIRCLES_SLIDE=1
endif
ifeq ($(TEST),1)
TARGET = CIRCLES-TEST
C_DEFS += -DCIRCLES_TEST=1 -DCIRCLES_SLIDE=1
endif

# The prebuilt libDaisy and DaisySP that come with TAPE. Rebuilding them with
# a different compiler is what breaks the SD card, so use these.
CHOMPI_LIBS ?= ../../chompi-tape/code/libs

DEBUG = 0
OPT = -O3
CPP_STANDARD = -std=gnu++17

APP_TYPE = BOOT_SRAM
LDSCRIPT = chompi_sram.lds

CPP_SOURCES = meter_main.cpp \
	board/encoder.cpp

C_INCLUDES += -Iboard -I$(CHOMPI_LIBS)/libDaisy/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc

LIBDAISY_DIR = $(CHOMPI_LIBS)/libDaisy
DAISYSP_DIR = $(CHOMPI_LIBS)/DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
