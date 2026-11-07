#pragma once
// Host stand-in for src/board/hardware.h: the real switch ids (taken from it by
// the Makefile, so they cannot drift) and a panel the tests press by hand.
#include <cstddef>
#include <cstdint>
namespace chompi {
struct Hardware {
    enum class SwId {
#include "swid.inc"
    };
    struct Enc {
        int inc = 0;
        int Increment() { int i = inc; inc = 0; return i; }
        bool RisingEdge() { return false; }
        bool FallingEdge() { return false; }
        bool Pressed() { return false; }
    } enc[8];
    struct SR {
        bool st[64] = {};
        bool State(int i) const { return st[i]; }
    } button_sr;
    bool toggle = true; // true: one channel (the toggle's bit down); false: all
    bool GetToggleState() { return toggle; }
    void Press(SwId s, bool down) { button_sr.st[int(s)] = down; }
};
} // namespace chompi
