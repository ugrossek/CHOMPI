# Host tests for the meter instrument (no hardware):
#   make run    build and run ./style_test
# Meter.h includes board/hardware.h relative to itself, so the test builds from
# a copy (stub/Meter_host.h) whose paths are fixed up to find the stubs.
CXX ?= g++
SRC = ../src/Meter.h ../src/Voice.h ../dsp/*.h ../study/*.h

stub/board/swid.inc: ../src/board/hardware.h
	sed -n '/enum class SwId/,/^        };/p' $< | sed '1,2d;$$d' > $@

stub/Meter_host.h: ../src/Meter.h
	sed -e 's|#include "\.\./dsp/|#include "../../dsp/|' $< > $@

style_test: style_test.cpp stub/board/swid.inc stub/Meter_host.h $(SRC)
	$(CXX) -std=c++17 -O1 -Wall -Wno-unused -Istub -I../src -o $@ style_test.cpp

run: style_test
	./style_test
clean:
	rm -f style_test stub/board/swid.inc stub/Meter_host.h
.PHONY: run clean
