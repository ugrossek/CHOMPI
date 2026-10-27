#pragma once
// The meter instrument: 15 channels, each a mallet sound; the keys play the
// selected channel. Loops are recorded afterwards: loop turns what you just
// played on a channel (its last loop length) into that channel's loop.
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

// A colour per preset, so a channel's sound shows on its key: ten hues as far
// apart as the LEDs allow.
static const float kPresetColor[kNumMalletPresets][3] = {
    {1.0f, .35f, .00f}, // wood: orange
    {1.0f, .00f, .00f}, // marimba: red
    {.00f, .00f, 1.0f}, // vibes: blue
    {1.0f, .85f, .00f}, // bronze: yellow
    {.00f, 1.0f, 1.0f}, // glass: cyan
    {1.0f, 1.0f, 1.0f}, // bell: white
    {.45f, .00f, 1.0f}, // gong: purple
    {1.0f, .00f, .60f}, // felt: pink
    {.00f, 1.0f, .00f}, // snare bar: green
    {.55f, 1.0f, .00f}, // muffled: lime
};

// Loop lengths as ratios of L0, one per black key from the lowest.
constexpr int kNumRatios = 10, kRatioOne = 4;
static const float kRatios[kNumRatios] = {1.f / 3, 1.f / 2, 2.f / 3, 3.f / 4, 1.f,
                                          5.f / 4, 4.f / 3, 3.f / 2, 2.f, 3.f};

static float MidiToHz(float n) { return 440.f * std::pow(2.f, (n - 69.f) / 12.f); }
static float Clamp01(float x) { return std::min(std::max(x, 0.f), 1.f); }

struct Channel {
    int preset = 0;
    MalletParams p;
    float level = .8f, pan = .5f;
    int octave = 0;
    int last_note = 60; // the last key played, before the octave: the preview note
    int ratio = kRatioOne; // index into kRatios: the loop length is L0 times this
};

// A channel's loop: notes at fixed places (phases, 0..1) in its loop length,
// so they keep their places when L0 or the ratio changes.
struct LoopNote { float phase; int note; };
constexpr int kLoopNotes = 32;
struct Loop {
    LoopNote n[kLoopNotes];
    int count = 0;
    float pos = 0.f; // where the channel is in its loop, 0..1
};

// What was played by hand on a channel, newest last; loop takes from here.
struct Played { uint32_t t; int note; };
constexpr int kHistory = 48;
struct History {
    Played e[kHistory];
    int head = 0, count = 0;
    uint32_t since = 0; // only notes after the last loop press (or clear) count
};

class Instrument {
public:
    void Init(float sample_rate) {
        sr_ = sample_rate;
        l0_ = 2.f * sample_rate;
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
    void Controls(Hardware& hw, size_t block) {
        auto& sr = hw.button_sr;
        const bool chompi = sr.State(int(Sw::KEY_26));
        all_ = !hw.GetToggleState();

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
                } else if (chompi) {
                    // CHOMPI + black key: the ratio of the selected channel, or of all.
                    if (all_) for (Channel& c : ch_) c.ratio = BlackIndex(k);
                    else ch_[sel_].ratio = BlackIndex(k);
                } else {
                    Channel& c = ch_[sel_];
                    c.last_note = kKeys[k].note;
                    const int note = kKeys[k].note + 12 * c.octave;
                    NoteOn(sel_, note);
                    Remember(sel_, note);
                }
            }
        }

        // Knob pushes: switch between the two pages.
        for (int n = 0; n < 4; ++n)
            if (Pressed(kKnobs[n].push)) page_ ^= 1;

        // Sample knobs.
        for (int n = 0; n < 4; ++n) {
            const int inc = hw.enc[kKnobs[n].enc].Increment();
            if (inc) Turn(n, inc, chompi);
        }
        // Wheel: the tempo, L0, smoothly (right is faster); every loop follows.
        // CHOMPI + turn: the selected channel's preset, heard as a preview once
        // the wheel rests; with the switch on all: every ratio a step up or down.
        if (const int inc = hw.enc[kKnobs[kTransport].enc].Increment()) {
            if (!chompi) {
                l0_ = std::min(std::max(l0_ * std::pow(kTempoStep, float(-inc)), kMinL0 * sr_), kMaxL0 * sr_);
            } else if (all_) {
                if (const int step = Steps(inc, -1))
                    for (Channel& c : ch_) c.ratio = std::min(std::max(c.ratio + step, 0), kNumRatios - 1);
            } else {
                Channel& c = ch_[sel_];
                c.preset = (c.preset + (inc > 0 ? 1 : kNumMalletPresets - 1)) % kNumMalletPresets;
                c.p = kMalletPresets[c.preset].p;
                Update(sel_);
                preview_ch_ = sel_;
                preview_wait_ = kPreviewBlocks;
            }
        }
        if (step_idle_ > 0) step_idle_--;
        if (oct_show_ > 0) oct_show_--;
        if (preview_wait_ > 0 && --preview_wait_ == 0) {
            const Channel& c = ch_[preview_ch_];
            NoteOn(preview_ch_, c.last_note + 12 * c.octave);
        }
        // Right knob: volume; CHOMPI + turn: the room.
        if (const int inc = hw.enc[kKnobs[kVolume].enc].Increment()) {
            if (chompi) room_ = Clamp01(room_ + inc * .02f);
            else volume_ = Clamp01(volume_ + inc * .02f);
        }

        // Right knob held: show voices and load.
        show_load_ = sr.State(kKnobs[kVolume].push);

        // Play: start and stop all loops. CHOMPI + play, tapped: L0 is the time
        // between two taps.
        if (Pressed(int(Sw::KEY_27))) {
            if (chompi) {
                const uint32_t gap = now_ - last_tap_;
                if (have_tap_ && gap >= kMinL0 * sr_ && gap <= kMaxL0 * sr_) l0_ = float(gap);
                last_tap_ = now_;
                have_tap_ = true;
            } else {
                running_ = !running_;
            }
        }

        // Loop: a short press turns what was just played on the selected channel
        // into its loop (CHOMPI + loop: adds to the loop). Held 1 s: clears it.
        if (Pressed(int(Sw::KEY_28))) {
            loop_down_ = true;
            loop_hold_ = 0;
            loop_ch_ = sel_;
            loop_add_ = chompi;
            loop_t_ = now_;
        }
        if (loop_down_) {
            if (sr.State(int(Sw::KEY_28))) {
                if (++loop_hold_ == kClearBlocks) { // the switch on all: clears every loop
                    for (int c = 0; c < kChannels; ++c) {
                        if (!all_ && c != loop_ch_) continue;
                        loop_[c].count = 0;
                        hist_[c].since = now_;
                        ch_[c].ratio = kRatioOne; // a cleared channel starts over at 1
                    }
                    loop_down_ = false;
                }
            } else {
                Capture(loop_ch_, loop_t_, loop_add_);
                loop_down_ = false;
            }
        }

        // The loops run: each note strikes again whenever its channel passes its place.
        if (running_) {
            l0_pos_ += block / l0_;
            if (l0_pos_ >= 1.f) { l0_pos_ -= 1.f; l0_flash_ = 1.f; }
            for (int c = 0; c < kChannels; ++c) Advance(c, block);
        }

        // Reboot: CHOMPI + lowest and highest white key, held 2 s, then let go.
        const bool combo = chompi && sr.State(int(Sw::KEY_1)) && sr.State(int(Sw::KEY_15));
        reboot_hold_ = combo ? reboot_hold_ + 1 : 0;
        if (reboot_hold_ > kRebootBlocks) reboot_armed_ = true;
        if (reboot_armed_ && !AnyKeyHeld(hw)) reboot_now_ = true;

        // Parameter changes reach the ringing voices once per block.
        for (int c = 0; c < kChannels; ++c)
            if (dirty_[c]) { dirty_[c] = false; UpdateVoices(c); }

        now_ += block;
    }

    // ---- Sound (audio callback) ----------------------------------------------
    void Process(float* l, float* r, size_t n) {
        for (int v = 0; v < kVoices; ++v) {
            Slot& s = slot_[v];
            if (!s.v.Active()) continue;
            const Channel& c = ch_[s.ch];
            // The ensemble's width spreads or gathers every channel around the middle.
            const float pan = Clamp01(.5f + (c.pan - .5f) * width_);
            const float level = c.level * all_level_;
            const float gl = level * std::cos(pan * float(M_PI) / 2.f);
            const float gr = level * std::sin(pan * float(M_PI) / 2.f);
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
                // Each flashes whenever its channel plays a note.
                const float* col = kPresetColor[ch_[c].preset];
                const float b = (c == sel_ ? .6f : .06f) + .4f * note_flash_[c];
                for (int j = 0; j < 3; ++j) rgb[j] = col[j] * b;
            } else if (chompi) {
                // The ratios on the black keys: the selected channel's bright.
                rgb[0] = rgb[1] = rgb[2] = BlackIndex(k) == ch_[sel_].ratio ? 1.f : .08f;
            } else if (oct_show_ > 0 && OctaveKey(k) >= -2) {
                // The octave on the five lowest black keys, -2 .. +2: the current one bright.
                const float b = OctaveKey(k) == ch_[sel_].octave ? 1.f : .1f;
                rgb[0] = rgb[1] = rgb[2] = b;
            } else {
                for (int j = 0; j < 3; ++j) rgb[j] = sel_col[j] * key_flash_[k];
            }
            SetSmtLedFloat(kKeys[k].led, rgb[0], rgb[1], rgb[2]);
            key_flash_[k] *= std::exp(-dt / .25f);
        }

        // Knobs: the channel's colour, warm white with the switch on all; page 2 paler.
        static constexpr float kWarm[3] = {1.f, .7f, .4f};
        const float* kc = all_ ? kWarm : sel_col;
        const float w = page_ ? .5f : 0.f;
        for (int n = 0; n < 4; ++n)
            SetPthLedFloat(kKnobs[n].led, kc[0] + (1.f - kc[0]) * w, kc[1] + (1.f - kc[1]) * w,
                           kc[2] + (1.f - kc[2]) * w);
        SetPthLedFloat(kKnobs[kTransport].led, sel_col[0], sel_col[1], sel_col[2]);
        SetPthLedFloat(kKnobs[kVolume].led, .3f * volume_, .3f * volume_, .3f * volume_);

        // Play: lit while the loops run, a beat on every L0. Loop: a beat on
        // every loop of the selected channel, brighter when it has a loop.
        const float pb = running_ ? .15f + .85f * l0_flash_ : 0.f;
        SetPthLedFloat(kPlayLed, 0.f, pb, 0.f);
        const float lb = (loop_[sel_].count ? .25f : .03f) + (running_ ? .6f * ch_flash_[sel_] : 0.f);
        SetPthLedFloat(kLoopLed, sel_col[0] * lb, sel_col[1] * lb, sel_col[2] * lb);
        fill_led_data();

        const float fade = std::exp(-dt / .15f);
        l0_flash_ *= fade;
        for (float& f : ch_flash_) f *= fade;
        const float note_fade = std::exp(-dt / .1f);
        for (float& f : note_flash_) f *= note_fade;
    }

private:
    struct Slot { MalletVoice v; int ch, note; uint32_t age = 0; };

    static constexpr int kClicksPerOctave = 4;
    static constexpr int kOctaveIdleBlocks = 1000; // clicks count towards a step for 1 s
    static constexpr int kOctaveShowBlocks = 1000; // the octave stays on the keys 1 s
    static constexpr int kPreviewBlocks = 150;     // the wheel rests 150 ms: preview

    // C# D# F# G# A#, the five lowest black keys, show octaves -2 .. +2.
    static int OctaveKey(int k) {
        switch (k) {
            case 1: return -2; case 3: return -1; case 6: return 0; case 8: return 1; case 10: return 2;
            default: return -99;
        }
    }
    static constexpr int kRebootBlocks = 2000; // 2 s of 1 ms blocks
    static constexpr float kGreen[3] = {0.f, 1.f, 0.f};
    static constexpr float kYellow[3] = {1.f, .8f, 0.f};
    static constexpr float kRed[3] = {1.f, 0.f, 0.f};

    static constexpr int kClearBlocks = 1000;     // loop held 1 s: clear
    static constexpr float kMinL0 = .5f, kMaxL0 = 8.f; // seconds
    static constexpr float kTempoStep = 1.01f;         // the wheel: 1% per click

    static int BlackIndex(int k) {
        int b = 0;
        for (int i = 0; i < k; ++i) b += !kKeys[i].white;
        return b;
    }

    float LoopSamples(int c) const { return l0_ * kRatios[ch_[c].ratio]; }

    void Remember(int c, int note) {
        History& h = hist_[c];
        h.e[h.head] = {now_, note};
        h.head = (h.head + 1) % kHistory;
        if (h.count < kHistory) h.count++;
    }

    // Loop pressed at time t: the notes played on channel c in the loop length
    // before t, and since the last loop press, become its loop (or join it).
    void Capture(int c, uint32_t t, bool add) {
        History& h = hist_[c];
        Loop& lp = loop_[c];
        const float len = LoopSamples(c);
        LoopNote got[kLoopNotes];
        int n = 0;
        for (int i = 0; i < h.count; ++i) {
            const Played& e = h.e[(h.head - h.count + i + kHistory) % kHistory];
            if (int32_t(e.t - h.since) <= 0 || int32_t(t - e.t) < 0 || float(t - e.t) >= len) continue;
            // Its place: as far behind the loop's position now as it was played before now.
            float ph = lp.pos - float(now_ - e.t) / len;
            ph -= std::floor(ph);
            if (n < kLoopNotes) got[n++] = {ph, e.note};
            else { std::copy(got + 1, got + kLoopNotes, got); got[kLoopNotes - 1] = {ph, e.note}; }
        }
        if (n == 0) return; // nothing played: the loop stays as it is
        h.since = t;
        if (!add) lp.count = 0;
        for (int i = 0; i < n; ++i) {
            bool twin = false; // the same note at the same place is already there
            for (int j = 0; j < lp.count; ++j)
                if (lp.n[j].note == got[i].note && std::fabs(lp.n[j].phase - got[i].phase) < .01f) twin = true;
            if (!twin && lp.count < kLoopNotes) lp.n[lp.count++] = got[i];
        }
    }

    void Advance(int c, size_t block) {
        Loop& lp = loop_[c];
        const float from = lp.pos;
        float to = from + block / LoopSamples(c);
        const bool wrap = to >= 1.f;
        if (wrap) { to -= 1.f; ch_flash_[c] = 1.f; }
        lp.pos = to;
        for (int i = 0; i < lp.count; ++i) {
            const float ph = lp.n[i].phase;
            if (wrap ? (ph >= from || ph < to) : (ph >= from && ph < to)) {
                NoteOn(c, lp.n[i].note);
                if (c == sel_) FlashNote(lp.n[i].note - 12 * ch_[c].octave);
            }
        }
    }

    void FlashNote(int note) {
        for (int k = 0; k < 25; ++k)
            if (kKeys[k].note == note) key_flash_[k] = 1.f;
    }

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
        p.material = Clamp01(p.material + off_material_);
        p.brightness = Clamp01(p.brightness + off_bright_);
        p.decay = Clamp01(p.decay + off_decay_);
        p.strike = Clamp01(p.strike + off_strike_);
        p.shimmer = Clamp01(p.shimmer + off_shimmer_);
        p.buzz = Clamp01(p.buzz + off_buzz_);
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
        note_flash_[c] = 1.f;
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

    // Knobs 1-4. Page 1: material, brightness, decay | level (CHOMPI: pan).
    // Page 2: strike (CHOMPI: variation), shimmer, buzz (CHOMPI: its kind) | octave.
    // With the switch on all: the same knobs as offsets on every channel, so each
    // keeps its character; level of all, CHOMPI + level: the stereo width.
    void Turn(int knob, int inc, bool chompi) {
        const float d = inc * .02f;
        if (all_) {
            if (page_ == 0) {
                switch (knob) {
                    case 0: Offset(off_material_, d); break;
                    case 1: Offset(off_bright_, d); break;
                    case 2: Offset(off_decay_, d); break;
                    case 3:
                        if (chompi) width_ = std::min(std::max(width_ + 2.f * d, 0.f), 2.f);
                        else all_level_ = std::min(std::max(all_level_ + d, 0.f), 1.5f);
                        return;
                }
            } else {
                switch (knob) {
                    case 0: Offset(off_strike_, d); break;
                    case 1: Offset(off_shimmer_, d); break;
                    case 2: Offset(off_buzz_, d); break;
                    case 3: return; // the octave is per channel
                }
            }
            UpdateAll();
            return;
        }
        Channel& c = ch_[sel_];
        MalletParams& p = c.p;
        if (page_ == 0) {
            switch (knob) {
                case 0: p.material = Clamp01(p.material + d); break;
                case 1: p.brightness = Clamp01(p.brightness + d); break;
                case 2: p.decay = Clamp01(p.decay + d); break;
                case 3:
                    if (chompi) c.pan = Clamp01(c.pan + d);
                    else c.level = Clamp01(c.level + d);
                    return;
            }
        } else {
            switch (knob) {
                case 0:
                    if (chompi) p.variation = Clamp01(p.variation + d);
                    else p.strike = Clamp01(p.strike + d);
                    break;
                case 1: p.shimmer = Clamp01(p.shimmer + d); break;
                case 2: // buzz; CHOMPI + turn: its kind (left rattle, right muffle)
                    if (chompi) p.buzz_kind = inc > 0 ? 1 : 0;
                    else p.buzz = Clamp01(p.buzz + d);
                    break;
                case 3: // the octave, a step every few clicks, shown on the black keys
                    if (const int step = Steps(inc, sel_)) c.octave = std::min(std::max(c.octave + step, -2), 2);
                    oct_show_ = kOctaveShowBlocks;
                    return;
            }
        }
        Update(sel_);
    }

    static void Offset(float& off, float d) { off = std::min(std::max(off + d, -.5f), .5f); }

    // Clicks that only count every few: a step (+1 or -1) once enough have
    // gathered on the same thing (id) within a second, else 0.
    int Steps(int inc, int id) {
        if (step_idle_ == 0 || step_id_ != id) step_clicks_ = 0;
        step_id_ = id;
        step_idle_ = kOctaveIdleBlocks;
        step_clicks_ += inc;
        if (std::abs(step_clicks_) < kClicksPerOctave) return 0;
        const int step = step_clicks_ > 0 ? 1 : -1;
        step_clicks_ = 0;
        return step;
    }

    static constexpr int kSwitches = 40; // 5 shift registers of 8
    bool Pressed(int i) const { return (pressed_ >> i) & 1; }
    uint64_t held_ = 0, pressed_ = 0;

    float sr_ = 48000.f;
    Channel ch_[kChannels];
    Slot slot_[kVoices];
    bool dirty_[kChannels] = {};
    uint32_t clock_ = 0;
    int sel_ = 0, page_ = 0;
    bool all_ = false, show_load_ = false;
    float off_material_ = 0.f, off_bright_ = 0.f, off_decay_ = 0.f;
    float off_strike_ = 0.f, off_shimmer_ = 0.f, off_buzz_ = 0.f;
    float all_level_ = 1.f;
    float room_ = .5f, width_ = 1.f, volume_ = .8f, load_ = 0.f;
    float key_flash_[25] = {}, note_flash_[kChannels] = {};
    int step_clicks_ = 0, step_id_ = 0, step_idle_ = 0, oct_show_ = 0;
    int preview_ch_ = 0, preview_wait_ = 0;
    int reboot_hold_ = 0;

    Loop loop_[kChannels];
    History hist_[kChannels];
    uint32_t now_ = 0;      // samples since start
    float l0_ = 96000.f;    // L0 in samples (2 s)
    float l0_pos_ = 0.f, l0_flash_ = 0.f, ch_flash_[kChannels] = {};
    bool running_ = true;
    uint32_t last_tap_ = 0;
    bool have_tap_ = false;
    bool loop_down_ = false, loop_add_ = false;
    int loop_hold_ = 0, loop_ch_ = 0;
    uint32_t loop_t_ = 0;
    bool reboot_armed_ = false, reboot_now_ = false;
};

} // namespace meter
