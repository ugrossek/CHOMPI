# PC render tool for the meter engines.
#   make            build ./render
#   make wavs       render all scenes into OUT (default ~/chompi-debug/meter-render)

OUT ?= $(HOME)/chompi-debug/meter-render

render: render.cpp ../dsp/*.h
	g++ -std=c++17 -O2 -Wall -o $@ render.cpp

wavs: render
	mkdir -p $(OUT)
	./render $(OUT)

clean:
	rm -f render

.PHONY: wavs clean
