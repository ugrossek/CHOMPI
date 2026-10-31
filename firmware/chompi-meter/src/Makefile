# meter firmware for the CHOMPI.
#   make            build build/CIRCLES.bin (GCC Arm 10.3-2021.10 on the PATH), the release
#   make TEST=1 BUILD_DIR=build-test
#                   build build-test/CIRCLES-TEST.bin, with what is still being
#                   tried: a figure's versions (loop + wheel) and choosing its
#                   length after playing (loop held + a black key)

TARGET = CIRCLES
ifeq ($(TEST),1)
TARGET = CIRCLES-TEST
C_DEFS += -DCIRCLES_TEST=1
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
