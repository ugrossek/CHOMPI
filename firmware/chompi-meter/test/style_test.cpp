// Host tests: the hidden style switch (CHOMPI tapped five times) and the acid
// house filter. Meter.h's state is reached by making private public.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>
#define private public
#include "Meter_host.h"
#undef private

using namespace meter;
using Sw = chompi::Hardware::SwId;

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static constexpr size_t kBlock = 48;
static constexpr float kSR = 48000.f;

struct Rig {
    chompi::Hardware hw;
    Instrument* in;
    std::vector<float> mem;
    Versions* ver;
    Rig() : mem(size_t(kVoices) * AnyVoice::kArenaFloats) {
        in = new Instrument();
        ver = new Versions();
        in->Init(kSR, ver, mem.data());
        // junk on inputs that are not keys must not matter
        hw.Press(Sw::NC_1, true); hw.Press(Sw::NC_4, true); hw.Press(Sw::ENC_2_SW, true);
        hw.Press(Sw::SW_TOG, true); // the toggle's bit is down in one position, as on the panel
    }
    ~Rig() { delete in; delete ver; }
    void Blocks(int n) { for (int i = 0; i < n; ++i) in->Controls(hw, kBlock); }
    void Ms(float ms) { Blocks(int(ms * kSR / 1000.f / kBlock + .5f)); }
    void Down(Sw s) { hw.Press(s, true); }
    void Up(Sw s) { hw.Press(s, false); }
    // CHOMPI down for hold_ms, then up and quiet for gap_ms.
    void Tap(float hold_ms = 80, float gap_ms = 100) {
        Down(Sw::KEY_26); Ms(hold_ms); Up(Sw::KEY_26); Ms(gap_ms);
    }
    int Style() const { return in->Style(); }
};

static void test_five_taps_in_and_back() {
    Rig r;
    r.Ms(50);
    CHECK(r.Style() == kStyleMallets);
    for (int i = 0; i < 4; ++i) { r.Tap(); CHECK(r.Style() == kStyleMallets); }
    r.Tap();
    CHECK(r.Style() == kStyleAcid);
    CHECK(std::fabs(r.in->l0_ - (240.f / 124.f) * kSR) < 1.f);   // a bar at 124 BPM
    CHECK(r.in->view_set_ == 1);
    CHECK(r.in->style_show_ > 0.f);
    for (int i = 0; i < 4; ++i) { r.Tap(); CHECK(r.Style() == kStyleAcid); }
    r.Tap();
    CHECK(r.Style() == kStyleMallets);
    CHECK(r.in->view_set_ == 0);
    // And in again: the count starts afresh each time.
    for (int i = 0; i < 5; ++i) r.Tap();
    CHECK(r.Style() == kStyleAcid);
    // Six taps is one switch, not two.
    Rig q;
    for (int i = 0; i < 6; ++i) q.Tap();
    CHECK(q.Style() == kStyleAcid);
    q.Tap();
    CHECK(q.Style() == kStyleAcid); // 6th counted as 1st of the next five
}

static void test_interrupted_taps() {
    { // a pause longer than the gap: the count starts over
        Rig r;
        for (int i = 0; i < 3; ++i) r.Tap();
        r.Ms(600);
        for (int i = 0; i < 2; ++i) r.Tap();
        CHECK(r.Style() == kStyleMallets);
        for (int i = 0; i < 3; ++i) r.Tap();
        CHECK(r.Style() == kStyleAcid);
    }
    { // a hold (too long to be a tap) resets
        Rig r;
        for (int i = 0; i < 3; ++i) r.Tap();
        r.Tap(500);
        for (int i = 0; i < 2; ++i) r.Tap();
        CHECK(r.Style() == kStyleMallets);
    }
    { // another key pressed between: resets
        Rig r;
        for (int i = 0; i < 3; ++i) r.Tap();
        r.Down(Sw::KEY_3); r.Ms(30); r.Up(Sw::KEY_3); r.Ms(30);
        r.Tap(); r.Tap();
        CHECK(r.Style() == kStyleMallets);
        for (int i = 0; i < 3; ++i) r.Tap();
        CHECK(r.Style() == kStyleAcid);
    }
    { // a tap with another key held: not a tap, and it resets
        Rig r;
        for (int i = 0; i < 4; ++i) r.Tap();
        r.Down(Sw::KEY_5);
        r.Tap();
        CHECK(r.Style() == kStyleMallets);
        r.Up(Sw::KEY_5); r.Ms(30);
        r.Tap();
        CHECK(r.Style() == kStyleMallets); // only the 1st of a new five
    }
    { // taps too slow (gap just over the limit) never add up
        Rig r;
        for (int i = 0; i < 10; ++i) r.Tap(80, 450);
        CHECK(r.Style() == kStyleMallets);
    }
    { // fast taps just inside the limits do
        Rig r;
        for (int i = 0; i < 5; ++i) r.Tap(250, 300);
        CHECK(r.Style() == kStyleAcid);
    }
}

// Fill the bus of every channel with the same noise and run the filter.
static float Energy(const float* x, size_t n) { double s = 0; for (size_t i = 0; i < n; ++i) s += x[i] * x[i]; return float(s); }

static bool IsDrum(Instrument& in, int c) { // the bass drum: the only one the filter leaves open
    return kMalletPresets[in.ch_[c].preset].engine == kEngKick;
}

static void test_filter() {
    Rig r;
    for (int i = 0; i < 5; ++i) r.Tap();
    CHECK(r.Style() == kStyleAcid);
    Instrument& in = *r.in;
    int drums = 0, others = 0;
    for (int c = 0; c < kChannels; ++c) { if (in.ch_[c].preset < 0) continue; IsDrum(in, c) ? ++drums : ++others; }
    CHECK(drums > 0 && others > 0);

    auto run = [&](float filt, std::vector<float>* in_bus, std::vector<float>* out_bus) {
        in.filter_ = filt; in.filter_set_ = -1.f;
        uint32_t seed = 1;
        for (int blk = 0; blk < 20; ++blk) {
            for (int c = 0; c < kChannels; ++c) {
                in.busy_[c] = true;
                for (size_t i = 0; i < 64; ++i) { seed = seed * 1664525u + 1013904223u; in.bus_[c][i] = (seed >> 8) / 8388608.f - 1.f; }
                if (in_bus) for (int c2 = 0; c2 < 1; ++c2) {}
            }
            // keep copies of what went in
            static float orig[kChannels][64];
            for (int c = 0; c < kChannels; ++c) std::memcpy(orig[c], in.bus_[c], sizeof orig[c]);
            in.Filter(64);
            if (blk == 19)
                for (int c = 0; c < kChannels; ++c) {
                    in_bus[c].assign(orig[c], orig[c] + 64);
                    out_bus[c].assign(in.bus_[c], in.bus_[c] + 64);
                }
        }
    };
    std::vector<float> ib[kChannels], ob[kChannels];

    // Closed: players are tamed, drums are bit-for-bit untouched.
    run(0.f, ib, ob);
    for (int c = 0; c < kChannels; ++c) {
        if (in.ch_[c].preset < 0) continue;
        if (IsDrum(in, c)) CHECK(std::memcmp(ib[c].data(), ob[c].data(), 64 * sizeof(float)) == 0);
        else CHECK(Energy(ob[c].data(), 64) < .2f * Energy(ib[c].data(), 64));
    }
    // Half: between.
    run(.5f, ib, ob);
    float closed_e = 0, half_e = 0;
    {
        std::vector<float> ib2[kChannels], ob2[kChannels];
        run(0.f, ib2, ob2);
        for (int c = 0; c < kChannels; ++c) if (in.ch_[c].preset >= 0 && !IsDrum(in, c)) { closed_e += Energy(ob2[c].data(), 64); half_e += Energy(ob[c].data(), 64); }
    }
    CHECK(half_e > closed_e);
    // Open: nothing is touched, and the filter forgets (so it starts clean when it closes).
    run(1.f, ib, ob);
    for (int c = 0; c < kChannels; ++c) if (in.ch_[c].preset >= 0) CHECK(std::memcmp(ib[c].data(), ob[c].data(), 64 * sizeof(float)) == 0);
    CHECK(in.filter_set_ < 0.f);
    // Drums stay open at every setting.
    for (float f : {0.f, .1f, .5f, .9f}) {
        run(f, ib, ob);
        for (int c = 0; c < kChannels; ++c)
            if (in.ch_[c].preset >= 0 && IsDrum(in, c)) CHECK(std::memcmp(ib[c].data(), ob[c].data(), 64 * sizeof(float)) == 0);
    }
}

static void Turn(Rig& r, int enc, int inc, bool chompi) {
    if (chompi) { r.Down(Sw::KEY_26); r.Ms(20); }
    r.hw.enc[enc].inc = inc; r.Blocks(1);
    if (chompi) { r.Up(Sw::KEY_26); r.Ms(20); }
}

static void test_knobs() {
    const int kK4 = kKnobs[3].enc, kVol = kKnobs[kVolume].enc;
    { // mallets: CHOMPI + K4 on all is the width; CHOMPI + volume the room
        Rig r; r.hw.toggle = false; r.Ms(20);
        CHECK(r.in->all_);
        const float size0 = r.in->size_;
        Turn(r, kK4, -5, true);
        CHECK(r.in->size_ < size0);
        CHECK(r.in->filter_ == 1.f);
        const float room0 = r.in->room_;
        Turn(r, kVol, -3, true);
        CHECK(r.in->room_ < room0);
    }
    { // acid, on all: CHOMPI + K4 is the filter (not the width); CHOMPI + volume the room again
        Rig r;
        for (int i = 0; i < 5; ++i) r.Tap();
        CHECK(r.Style() == kStyleAcid);
        r.hw.toggle = false; r.Ms(20);
        CHECK(r.in->all_);
        const float size0 = r.in->size_, gain0 = r.in->gain_, vol0 = r.in->volume_;
        Turn(r, kK4, -10, true);
        CHECK(r.in->filter_ < 1.f);
        CHECK(r.in->size_ == size0 && r.in->gain_ == gain0);
        const float f = r.in->filter_, room0 = r.in->room_;
        Turn(r, kVol, -3, true);
        CHECK(r.in->room_ < room0);
        CHECK(r.in->filter_ == f);
        CHECK(r.in->volume_ == vol0);
        Turn(r, kK4, -2, false);               // plain K4 on all: still the ensemble's level
        CHECK(r.in->gain_ < gain0 && r.in->filter_ == f);
        Turn(r, kVol, -1, false);
        CHECK(r.in->volume_ < vol0);
        // CHOMPI + push K4 takes the filter back open
        r.Down(Sw::KEY_26); r.Down(Sw::ENC_3_SW); r.Ms(20); r.Up(Sw::ENC_3_SW); r.Up(Sw::KEY_26); r.Ms(20);
        CHECK(r.in->filter_ == 1.f);
        // Leaving the style opens the filter.
        Turn(r, kK4, -10, true);
        for (int i = 0; i < 5; ++i) r.Tap();
        CHECK(r.Style() == kStyleMallets && r.in->filter_ == 1.f);
    }
    { // acid, one channel: CHOMPI + K4 is still that player's pan
        Rig r;
        for (int i = 0; i < 5; ++i) r.Tap();
        const float pan0 = r.in->ch_[r.in->sel_].pan;
        Turn(r, kK4, 5, true);
        CHECK(r.in->ch_[r.in->sel_].pan != pan0 && r.in->filter_ == 1.f);
    }
}

static void test_tempo() {
    Rig r;
    // Wherever the tempo was, entering acid house puts a bar at 124 BPM, leaving puts 2 s back.
    r.in->l0_ = 3.3f * kSR;
    for (int i = 0; i < 5; ++i) r.Tap();
    CHECK(std::fabs(r.in->L0Seconds() - 240.f / 124.f) < 1e-3f);
    for (int i = 0; i < 5; ++i) r.Tap();
    CHECK(std::fabs(r.in->L0Seconds() - 2.f) < 1e-3f);
}

int main() {
    test_five_taps_in_and_back();
    test_interrupted_taps();
    test_filter();
    test_knobs();
    test_tempo();
    std::printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
