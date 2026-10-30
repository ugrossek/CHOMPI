# meter firmware for the CHOMPI.
#   make            build build/CIRCLES.bin (GCC Arm 10.3-2021.10 on the PATH)

TARGET = CIRCLES

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
