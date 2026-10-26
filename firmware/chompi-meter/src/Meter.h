#pragma once
// The meter instrument, first hardware version: 15 channels, each a mallet
// sound; the keys play the selected channel. No latch or loops yet.
//
// Everything here runs in the audio callback (controls once per block, then
// the sound), except DrawLeds(), which the main loop calls.

#include "../dsp/Mallet.h"
#include "../dsp/Presets.h"
#include "board/hardware.h"
#include "board/temp_led_stuff.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace meter {

using chompi::Hardware;
using Sw = Hardware::SwId;

constexpr int kChannels = 15;
constexpr int kVoices = 32; // the pool all channels share; the load guard limits it in practice

// Keyboard: the shift register index of each key, low to high, its note and
// its LED (from TAPE's key and LED maps).
struct Key { Sw sw; int note; int led; bool white; };
static const Key kKeys[25] = {
    {Sw::KEY_1, 48, 24, true},  {Sw::KEY_16, 49, 0, false}, {Sw::KEY_2, 50, 23, true},
    {Sw::KEY_17, 51, 1, false}, {Sw::KEY_3, 52, 22, true},  {Sw::KEY_4, 53, 21, true},
    {Sw::KEY_18, 54, 2, false}, {Sw::KEY_5, 55, 20, true},  {Sw::KEY_19, 56, 3, false},
    {Sw::KEY_6, 57, 19, true},  {Sw::KEY_20, 58, 4, false}, {Sw::KEY_7, 59, 18, true},
    {Sw::KEY_8, 60, 17, true},  {Sw::KEY_21, 61, 5, false}, {Sw::KEY_9, 62, 16, true},
    {Sw::KEY_22, 63, 6, false}, {Sw::KEY_10, 64, 15, true}, {Sw::KEY_11, 65, 14, true},
    {Sw::KEY_23, 66, 7, false}, {Sw::KEY_12, 67, 13, true}, {Sw::KEY_24, 68, 8, false},
    {Sw::KEY_13, 69, 12, true}, {Sw::KEY_25, 70, 9, false}, {Sw::KEY_14, 71, 11, true},
    {Sw::KEY_15, 72, 10, true},
};

// The six knobs as they sit on the panel: four sample knobs, transport,
// volume. Index into Hardware::enc, the push switch, and the knob's LED.
struct Knob { int enc; int push; int led; };
static const Knob kKnobs[6] = {
    {3, int(Sw::ENC_4_SW), 1}, {0, int(Sw::ENC_1_SW), 2}, {1, int(Sw::ENC_2_SW), 3},
    {2, int(Sw::ENC_3_SW), 4}, {4, -1, 0},                {5, int(Sw::ENC_6_SW), 9},
};
constexpr int kTransport = 4, kVolume = 5;
constexpr int kPlayLed = 7, kLoopLed = 8;

// A colour per preset, so a channel's sound shows on its key.
static const float kPresetColor[kNumMalletPresets][3] = {
    {.80f, .40f, .10f}, // wood
    {1.0f, .25f, .05f}, // marimba
    {.30f, .50f, 1.0f}, // vibes
    {1.0f, .60f, .00f}, // bronze
    {.20f, 1.0f, .90f}, // glass
    {.90f, .90f, .50f}, // bell
    {1.0f, .80f, .20f}, // gong
    {.90f, .30f, .50f}, // felt
    {.50f, 1.0f, .10f}, // snare bar
    {.60f, .20f, 1.0f}, // muffled
};

static float MidiToHz(float n) { return 440.f * std::pow(2.f, (n - 69.f) / 12.f); }
static float Clamp01(float x) { return std::min(std::max(x, 0.f), 1.f); }

struct Channel {
    int preset = 0;
    MalletParams p;
    float level = .8f, pan = .5f;
    int octave = 0;
};

class Instrument {
public:
    void Init(float sample_rate) {
        sr_ = sample_rate;
        for (int c = 0; c < kChannels; ++c) {
            ch_[c].preset = c % kNumMalletPresets;
            ch_[c].p = kMalletPresets[ch_[c].preset].p;
            ch_[c].pan = .5f + .15f * std::sin(c * 2.1f); // spread across the stereo field
            ch_[c].octave = ch_[c].preset == 6 ? -1 : 0;  // the gong sounds best low
        }
        for (int v = 0; v < kVoices; ++v) {
            slot_[v].v.Init(sample_rate, v + 1);
            slot_[v].ch = slot_[v].note = -1;
        }
    }

    // ---- Controls (audio callback, once per block) --------------------------
    void Controls(Hardware& hw) {
        auto& sr = hw.button_sr;
        const bool chompi = sr.State(int(Sw::KEY_26));
        ensemble_ = !hw.GetToggleState();

        // Presses: a switch that is down now and was up last block. (The shift
        // register's own RisingEdge only re-arms after FallingEdge is asked.)
        uint64_t down = 0;
        for (int i = 0; i < kSwitches; ++i)
            if (sr.State(i)) down |= uint64_t(1) << i;
        pressed_ = down & ~held_;
        held_ = down;

        // Keys: CHOMPI + white key selects a channel, otherwise play.
        for (int k = 0; k < 25; ++k) {
            const int i = int(kKeys[k].sw);
            if (Pressed(i)) {
                key_flash_[k] = 1.f;
                if (chompi && kKeys[k].white) {
                    sel_ = WhiteIndex(k);
                } else {
                    const Channel& c = ch_[sel_];
                    NoteOn(sel_, kKeys[k].note + 12 * c.octave);
                }
            }
        }

        // Knob pushes: switch between the two pages of the channel view.
        for (int n = 0; n < 4; ++n)
            if (Pressed(kKnobs[n].push) && !ensemble_) page_ ^= 1;

        // Sample knobs.
        for (int n = 0; n < 4; ++n) {
            const int inc = hw.enc[kKnobs[n].enc].Increment();
            if (inc) Turn(n, inc, chompi);
        }
        // Transport: the selected channel's preset; CHOMPI + turn: its octave.
        if (const int inc = hw.enc[kKnobs[kTransport].enc].Increment()) {
            Channel& c = ch_[sel_];
            if (chompi) {
                c.octave = std::min(std::max(c.octave + inc, -2), 2);
            } else {
                c.preset = (c.preset + (inc > 0 ? 1 : kNumMalletPresets - 1)) % kNumMalletPresets;
                c.p = kMalletPresets[c.preset].p;
                Update(sel_);
            }
        }
        // Volume.
        if (const int inc = hw.enc[kKnobs[kVolume].enc].Increment())
            volume_ = Clamp01(volume_ + inc * .02f);

        // Play held: show voices and load. Loop: add a cluster (voice test).
        show_load_ = sr.State(int(Sw::KEY_27));
        if (Pressed(int(Sw::KEY_28))) {
            static const int kCluster[6] = {0, 7, 12, 16, 19, 24};
            for (int n : kCluster) NoteOn(sel_, 48 + n + 12 * ch_[sel_].octave + (cluster_++ % 12));
        }

        // Reboot: CHOMPI + lowest and highest white key, held 2 s, then let go.
        const bool combo = chompi && sr.State(int(Sw::KEY_1)) && sr.State(int(Sw::KEY_15));
        reboot_hold_ = combo ? reboot_hold_ + 1 : 0;
        if (reboot_hold_ > kRebootBlocks) reboot_armed_ = true;
        if (reboot_armed_ && !AnyKeyHeld(hw)) reboot_now_ = true;

        // Parameter changes reach the ringing voices once per block.
        for (int c = 0; c < kChannels; ++c)
            if (dirty_[c]) { dirty_[c] = false; UpdateVoices(c); }
    }

    // ---- Sound (audio callback) ----------------------------------------------
    void Process(float* l, float* r, size_t n) {
        for (int v = 0; v < kVoices; ++v) {
            Slot& s = slot_[v];
            if (!s.v.Active()) continue;
            const Channel& c = ch_[s.ch];
            // The ensemble's width spreads or gathers every channel around the middle.
            const float pan = Clamp01(.5f + (c.pan - .5f) * width_);
            const float gl = c.level * std::cos(pan * float(M_PI) / 2.f);
            const float gr = c.level * std::sin(pan * float(M_PI) / 2.f);
            for (size_t i = 0; i < n; ++i) {
                const float x = s.v.Process();
                l[i] += x * gl;
                r[i] += x * gr;
            }
        }
    }

    // Load guard: the callback is too slow, drop the oldest voice.
    void ShedVoice() {
        int oldest = -1;
        for (int v = 0; v < kVoices; ++v)
            if (slot_[v].v.Active() && (oldest < 0 || slot_[v].age < slot_[oldest].age)) oldest = v;
        if (oldest >= 0) slot_[oldest].v.Stop();
    }

    int ActiveVoices() const {
        int n = 0;
        for (const auto& s : slot_) n += s.v.Active();
        return n;
    }

    float Room() const { return room_; }
    float Volume() const { return volume_; }
    bool RebootNow() const { return reboot_now_; }
    void SetLoad(float load) { load_ = load; }

    // ---- LEDs (main loop) ------------------------------------------------------
    void DrawLeds(Hardware& hw, float dt) {
        using namespace chompi;
        const bool chompi = hw.button_sr.State(int(Sw::KEY_26));
        const float* sel_col = kPresetColor[ch_[sel_].preset];

        for (int k = 0; k < 25; ++k) {
            float rgb[3] = {0.f, 0.f, 0.f};
            if (reboot_armed_) {
                rgb[0] = rgb[1] = rgb[2] = 1.f;
            } else if (show_load_) {
                // A bar of active voices from the lowest key; green, yellow, red by load.
                if (k < ActiveVoices()) {
                    const float* col = load_ < .6f ? kGreen : load_ < .85f ? kYellow : kRed;
                    std::copy(col, col + 3, rgb);
                }
            } else if (chompi && kKeys[k].white) {
                // The channels: each white key in its channel's colour, the selected one bright.
                const int c = WhiteIndex(k);
                const float* col = kPresetColor[ch_[c].preset];
                const float b = c == sel_ ? 1.f : .15f;
                for (int j = 0; j < 3; ++j) rgb[j] = col[j] * b;
            } else {
                for (int j = 0; j < 3; ++j) rgb[j] = sel_col[j] * key_flash_[k];
            }
            SetSmtLedFloat(kKeys[k].led, rgb[0], rgb[1], rgb[2]);
            key_flash_[k] *= std::exp(-dt / .25f);
        }

        // Knobs: the channel's colour (page 2 paler), warm white for the ensemble.
        for (int n = 0; n < 4; ++n) {
            if (ensemble_) {
                SetPthLedFloat(kKnobs[n].led, 1.f, .7f, .4f);
            } else {
                const float w = page_ ? .5f : 0.f;
                SetPthLedFloat(kKnobs[n].led, sel_col[0] + (1.f - sel_col[0]) * w,
                               sel_col[1] + (1.f - sel_col[1]) * w, sel_col[2] + (1.f - sel_col[2]) * w);
            }
        }
        SetPthLedFloat(kKnobs[kTransport].led, sel_col[0], sel_col[1], sel_col[2]);
        SetPthLedFloat(kKnobs[kVolume].led, .3f * volume_, .3f * volume_, .3f * volume_);
        fill_led_data();
    }

private:
    struct Slot { MalletVoice v; int ch, note; uint32_t age = 0; };

    static constexpr int kRebootBlocks = 2000; // 2 s of 1 ms blocks
    static constexpr float kGreen[3] = {0.f, 1.f, 0.f};
    static constexpr float kYellow[3] = {1.f, .8f, 0.f};
    static constexpr float kRed[3] = {1.f, 0.f, 0.f};

    static int WhiteIndex(int k) {
        int w = 0;
        for (int i = 0; i < k; ++i) w += kKeys[i].white;
        return w;
    }

    static bool AnyKeyHeld(Hardware& hw) {
        for (const Key& k : kKeys)
            if (hw.button_sr.State(int(k.sw))) return true;
        return hw.button_sr.State(int(Sw::KEY_26)) || hw.button_sr.State(int(Sw::KEY_27))
               || hw.button_sr.State(int(Sw::KEY_28));
    }

    MalletParams Effective(int c) const {
        MalletParams p = ch_[c].p;
        p.brightness = Clamp01(p.brightness + off_bright_);
        p.decay = Clamp01(p.decay + off_decay_);
        p.strike = Clamp01(p.strike + off_strike_);
        return p;
    }

    void NoteOn(int c, int note) {
        // The same key on the same channel strikes its ringing voice again.
        int pick = -1;
        for (int v = 0; v < kVoices && pick < 0; ++v)
            if (slot_[v].v.Active() && slot_[v].ch == c && slot_[v].note == note) pick = v;
        for (int v = 0; v < kVoices && pick < 0; ++v)
            if (!slot_[v].v.Active()) pick = v;
        if (pick < 0) { // steal the oldest
            pick = 0;
            for (int v = 1; v < kVoices; ++v)
                if (slot_[v].age < slot_[pick].age) pick = v;
        }
        Slot& s = slot_[pick];
        s.ch = c;
        s.note = note;
        s.age = ++clock_;
        s.v.Strike(MidiToHz(float(note)), Effective(c));
    }

    void Update(int c) { dirty_[c] = true; }
    void UpdateAll() { for (int c = 0; c < kChannels; ++c) dirty_[c] = true; }

    void UpdateVoices(int c) {
        const MalletParams p = Effective(c);
        for (auto& s : slot_)
            if (s.v.Active() && s.ch == c) s.v.SetParams(p);
    }

    void Turn(int knob, int inc, bool chompi) {
        const float d = inc * .02f;
        if (ensemble_) {
            // The ensemble: offsets on every channel, each keeps its character.
            switch (knob) {
                case 0: off_bright_ = std::min(std::max(off_bright_ + d, -.5f), .5f); break;
                case 1: off_decay_ = std::min(std::max(off_decay_ + d, -.5f), .5f); break;
                case 2: off_strike_ = std::min(std::max(off_strike_ + d, -.5f), .5f); break;
                case 3: // room; CHOMPI + turn: stereo width (mono .. twice the spread)
                    if (chompi) width_ = std::min(std::max(width_ + 2.f * d, 0.f), 2.f);
                    else room_ = Clamp01(room_ + d);
                    return;
            }
            UpdateAll();
            return;
        }
        Channel& c = ch_[sel_];
        MalletParams& p = c.p;
        if (page_ == 0) {
            float* f[4] = {&p.material, &p.brightness, &p.decay, &p.strike};
            *f[knob] = Clamp01(*f[knob] + d);
        } else {
            switch (knob) {
                case 0: p.variation = Clamp01(p.variation + d); break;
                case 1: p.shimmer = Clamp01(p.shimmer + d); break;
                case 2: // buzz; CHOMPI + turn: its kind (left rattle, right muffle)
                    if (chompi) p.buzz_kind = inc > 0 ? 1 : 0;
                    else p.buzz = Clamp01(p.buzz + d);
                    break;
                case 3: // level; CHOMPI + turn: pan
                    if (chompi) c.pan = Clamp01(c.pan + d);
                    else c.level = Clamp01(c.level + d);
                    return;
            }
        }
        Update(sel_);
    }

    static constexpr int kSwitches = 40; // 5 shift registers of 8
    bool Pressed(int i) const { return (pressed_ >> i) & 1; }
    uint64_t held_ = 0, pressed_ = 0;

    float sr_ = 48000.f;
    Channel ch_[kChannels];
    Slot slot_[kVoices];
    bool dirty_[kChannels] = {};
    uint32_t clock_ = 0;
    int sel_ = 0, page_ = 0, cluster_ = 0;
    bool ensemble_ = false, show_load_ = false;
    float off_bright_ = 0.f, off_decay_ = 0.f, off_strike_ = 0.f;
    float room_ = .5f, width_ = 1.f, volume_ = .8f, load_ = 0.f;
    float key_flash_[25] = {};
    int reboot_hold_ = 0;
    bool reboot_armed_ = false, reboot_now_ = false;
};

} // namespace meter
