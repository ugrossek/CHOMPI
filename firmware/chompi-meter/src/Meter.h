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
constexpr int kWheelLeds[2] = {5, 6};

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
// note: without the channel's octave, which is added when it plays (the loop
// follows the octave); jit: this repeat lands a little early or late (the
// feel); armed: not yet played in this round.
struct LoopNote { float phase; int note; float jit = 0.f; bool armed = true; };
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
        Start();
        for (int i = 0; i < kNumMalletPresets; ++i)
            preset_gain_[i] = std::pow(10.f, kMalletPresets[i].gain_db / 20.f);
        for (int v = 0; v < kVoices; ++v) {
            slot_[v].v.Init(sample_rate, v + 1);
            slot_[v].ch = slot_[v].note = -1;
        }
    }

    // As at power-on: every channel on its own sound, no loops, everything at
    // its start. (The volume and the room stay as they are.)
    void Start() {
        l0_ = 2.f * sr_;
        for (int c = 0; c < kChannels; ++c) {
            ch_[c] = Channel();
            ch_[c].preset = c % kNumMalletPresets;
            ch_[c].p = kMalletPresets[ch_[c].preset].p;
            // Spread across the stereo field: 1 in the middle, then outwards,
            // neighbours on opposite sides (2 left, 3 right, 4 further left ...).
            const int out = (c + 1) / 2;
            ch_[c].pan = .5f + (c & 1 ? -.35f : .35f) * out / 7.f;
            ch_[c].octave = ch_[c].preset == 6 ? -1 : 0;  // the gong sounds best low
            loop_[c].count = 0;
            hist_[c].since = now_;
            pending_[c] = -1;
            muted_[c] = false;
            swell_[c] = 1.f;
            dirty_[c] = true;
        }
        sel_ = 0;
        running_ = true;
        ens_ = kRatioOne;
        pending_ens_ = -1;
        off_bright_ = off_decay_ = off_strike_ = off_shimmer_ = off_buzz_ = off_variation_ = 0.f;
        width_ = 1.f;
        turn_ = 0.f;
        Restart();
    }

    // Every loop from its start, together, on a new L0 beat.
    void Restart() {
        l0_pos_ = 0.f;
        for (int c = 0; c < kChannels; ++c) {
            loop_[c].pos = 0.f;
            for (int i = 0; i < loop_[c].count; ++i) {
                loop_[c].n[i].armed = true;
                loop_[c].n[i].jit = 0.f;
            }
        }
    }

    // ---- Controls (audio callback, once per block) --------------------------
    void Controls(Hardware& hw, size_t block) {
        auto& sr = hw.button_sr;
        const bool chompi = sr.State(int(Sw::KEY_26));
        const bool all = !hw.GetToggleState();
        if (all != all_) Event(kEvSwitch, kIdSwitch, all);
        all_ = all;

        // A message from the page, waiting since the last block.
        if (mail_ready_) {
            Apply(mail_, mail_len_);
            __sync_synchronize();
            mail_ready_ = false;
        }

        // Presses: a switch that is down now and was up last block. (The shift
        // register's own RisingEdge only re-arms after FallingEdge is asked.)
        uint64_t down = 0;
        for (int i = 0; i < kSwitches; ++i)
            if (sr.State(i)) down |= uint64_t(1) << i;
        pressed_ = down & ~held_;
        const uint64_t released = held_ & ~down;
        held_ = down;
        for (int id = 0; id < kIdKnob; ++id) {
            const int i = SwitchOf(id);
            if ((pressed_ >> i) & 1) Event(kEvPress, id);
            if ((released >> i) & 1) Event(kEvRelease, id);
        }

        // Keys: CHOMPI + white key selects a channel (tapped twice: its sound on
        // the white keys); play + white key held listens to that channel,
        // CHOMPI + play + white key mutes it; otherwise the key plays.
        const bool play_held = sr.State(int(Sw::KEY_27)), loop_held = sr.State(int(Sw::KEY_28));
        if (!chompi) { sound_view_ = false; tap_key_ = -1; }
        for (int k = 0; k < 25; ++k) {
            const int i = int(kKeys[k].sw);
            if (Pressed(i)) {
                key_flash_[k] = 1.f;
                if (play_held) {
                    if (kKeys[k].white) {
                        if (chompi) muted_[WhiteIndex(k)] ^= 1;
                        else listen_ |= 1u << WhiteIndex(k);
                    }
                    play_used_ = true;
                } else if (sound_view_ && kKeys[k].white) {
                    // The sounds on the first white keys: pick one, heard at once.
                    if (WhiteIndex(k) < kNumMalletPresets) {
                        Channel& c = ch_[sel_];
                        c.preset = WhiteIndex(k);
                        c.p = kMalletPresets[c.preset].p;
                        Update(sel_);
                        NoteOn(sel_, c.last_note + 12 * c.octave);
                    }
                } else if (chompi && kKeys[k].white) {
                    // A second white key while one is still held: copy that channel's
                    // pattern here. The same key tapped twice while CHOMPI stays
                    // down: its sound (on letting go, so a copy never opens it).
                    const int from = HeldWhite(k);
                    if (from >= 0) {
                        Copy(from, WhiteIndex(k));
                        tap_key_ = -1;
                    } else {
                        tap_again_ = k == tap_key_;
                        tap_key_ = k;
                    }
                    sel_ = WhiteIndex(k);
                } else if (chompi) {
                    // CHOMPI + black key: the selected channel's ratio, from the next
                    // L0 beat, where its loop starts again (its own ratio again: back
                    // in step). With the switch on all: the ensemble's ratio, a
                    // multiplier on every channel's length (1: as recorded).
                    if (all_) pending_ens_ = BlackIndex(k);
                    else pending_[sel_] = BlackIndex(k);
                    if (!running_) ApplyPending(); // stopped: no beat to wait for
                } else {
                    Channel& c = ch_[sel_];
                    c.last_note = kKeys[k].note;
                    NoteOn(sel_, kKeys[k].note + 12 * c.octave);
                    Remember(sel_, kKeys[k].note + 12 * c.octave);
                }
            }
        }

        if (tap_again_ && tap_key_ >= 0 && !sr.State(int(kKeys[tap_key_].sw))) {
            sound_view_ = true;
            tap_again_ = false;
        }

        // Knobs 1-4.
        for (int n = 0; n < 4; ++n) {
            const int inc = hw.enc[kKnobs[n].enc].Increment();
            if (inc) { Turn(n, inc, chompi); Event(kEvTurn, kIdKnob + n, inc); }
        }
        // Wheel: the selected channel's octave, a step every few clicks, shown on
        // the black keys (the keys stay free to try it). CHOMPI + turn: the
        // tempo, L0, smoothly (right is faster); every loop follows.
        if (const int inc = hw.enc[kKnobs[kTransport].enc].Increment()) {
            Event(kEvTurn, kIdKnob + kTransport, inc);
            if (chompi) {
                l0_ = std::min(std::max(l0_ * std::pow(kTempoStep, float(-inc)), kMinL0 * sr_), kMaxL0 * sr_);
            } else {
                if (const int step = Steps(inc, sel_)) ch_[sel_].octave = std::min(std::max(ch_[sel_].octave + step, -2), 2);
                oct_show_ = kOctaveShowBlocks;
            }
        }
        if (step_idle_ > 0) step_idle_--;
        if (oct_show_ > 0) oct_show_--;
        if (meter_wait_ > 0) meter_wait_--;
        // Right knob: volume; CHOMPI + turn: the room.
        if (const int inc = hw.enc[kKnobs[kVolume].enc].Increment()) {
            Event(kEvTurn, kIdKnob + kVolume, inc);
            float& v = chompi ? room_ : volume_;
            v = Clamp01(v + inc * .02f);
            ShowMeter(kMeterBar, v, -1.f, true);
        }

        // Right knob held: show voices and load.
        show_load_ = sr.State(kKnobs[kVolume].push);

        // Play, on release, unless it listened or muted (or CHOMPI was held):
        // starts and stops all loops.
        if (Pressed(int(Sw::KEY_27))) {
            play_down_ = true;
            play_used_ = chompi;
        }
        if (play_down_ && !play_held) {
            play_down_ = false;
            if (!play_used_) running_ = !running_;
        }
        // Listening lasts as long as play and the white key are held.
        if (!play_held) listen_ = 0;
        for (int k = 0; k < 25; ++k)
            if (kKeys[k].white && !sr.State(int(kKeys[k].sw))) listen_ &= ~(1u << WhiteIndex(k));

        // Loop: a press turns what was just played on the selected channel into
        // its loop, at once (CHOMPI + loop: adds to the loop). Held 2 s (the
        // light turns red after 1 s): clears the channel; CHOMPI + loop held
        // 2 s: clears every channel.
        if (Pressed(int(Sw::KEY_28))) {
            loop_down_ = true;
            loop_used_ = false;
            loop_hold_ = 0;
            loop_ch_ = sel_;
            loop_chompi_ = chompi;
            Capture(sel_, now_, chompi);
        }
        if (loop_down_) {
            if (!loop_held) {
                loop_down_ = false;
            } else if (++loop_hold_ == kClearBlocks && !loop_used_) {
                for (int c = 0; c < kChannels; ++c) {
                    if (!loop_chompi_ && c != loop_ch_) continue;
                    loop_[c].count = 0;
                    hist_[c].since = now_;
                    ch_[c].ratio = kRatioOne; // a cleared channel starts over at 1
                    pending_[c] = -1;
                }
                loop_used_ = true;
            }
        }

        // Mute and listen: a short fade, the loops keep running underneath. A
        // channel listened to plays (even when muted), the others step back.
        for (int c = 0; c < kChannels; ++c) {
            float target = muted_[c] ? 0.f : 1.f;
            if (listen_) target = listen_ & (1u << c) ? 1.f : target * kListenOthers;
            gate_[c] += (target - gate_[c]) * .2f;
        }

        // The loops run: each note strikes again whenever its channel passes its place.
        if (running_) {
            l0_pos_ += block / Beat();
            if (l0_pos_ >= 1.f) {
                l0_pos_ -= 1.f;
                l0_flash_ = 1.f;
                l0_beats_++;
                Event(kEvBeat, 0, int(l0_beats_ & 63));
                ApplyPending();
            }
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
            // The ensemble's field: width spreads or gathers the channels around
            // the middle, turn moves all of them to one side.
            const float pan = Clamp01(.5f + (c.pan - .5f) * width_ + turn_);
            const float level = c.level * preset_gain_[c.preset] * gate_[s.ch];
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

    // ---- The link (main loop): a page on a computer follows over USB-MIDI ------
    // Controls by id: the 25 keys low to high (0-24), CHOMPI 25, play 26, loop
    // 27, the knobs K1-K4 28-31, the wheel 32, K6 33, the switch 34.
    static constexpr int kIdChompi = 25, kIdKnob = 28, kIdSwitch = 34;
    enum EventKind { kEvPress = 1, kEvRelease, kEvTurn, kEvSwitch, kEvBeat };

    // The next input or beat, three 7-bit bytes: kind, id, value (a turn's
    // clicks + 64; the switch 1 on all; a beat its count).
    bool PopEvent(uint8_t* e) {
        if (ev_tail_ == ev_head_) return false;
        std::copy(ev_[ev_tail_], ev_[ev_tail_] + 3, e);
        __sync_synchronize();
        ev_tail_ = (ev_tail_ + 1) % kEvents;
        return true;
    }

    // Lights the controls in `mask` (a bit per id) for the page.
    void SetHint(uint64_t mask) { hint_ = mask; }

    // A message for the instrument (the page's command and what follows),
    // taken at the next block; false while the last one still waits.
    bool Post(const uint8_t* d, int n) {
        if (mail_ready_ || n > kMail) return false;
        std::copy(d, d + n, mail_);
        mail_len_ = n;
        __sync_synchronize();
        mail_ready_ = true;
        return true;
    }

    // The ensemble's state, as 7-bit bytes (see PROTOCOL.md); returns the length.
    int Describe(uint8_t* o) const {
        int n = 0;
        o[n++] = uint8_t(sel_);
        o[n++] = uint8_t(running_ | all_ << 1 | sound_view_ << 2 | (oct_show_ > 0) << 3
                         | (meter_wait_ > 0) << 4 | reboot_armed_ << 5);
        o[n++] = uint8_t(ens_);
        o[n++] = uint8_t(pending_ens_ + 1);
        const int ms = std::min(int(l0_ * 1000.f / sr_ + .5f), 16383);
        o[n++] = ms & 127;
        o[n++] = ms >> 7;
        o[n++] = Q7(volume_);
        o[n++] = Q7(room_);
        o[n++] = Q7(width_ * .5f);
        o[n++] = Q7(turn_ + .5f);
        for (float off : {off_strike_, off_bright_, off_decay_, off_shimmer_, off_variation_, off_buzz_})
            o[n++] = Q7(off + .5f);
        uint32_t muted = 0;
        for (int c = 0; c < kChannels; ++c) muted |= uint32_t(muted_[c]) << c;
        for (uint32_t m : {muted, listen_}) {
            o[n++] = m & 127;
            o[n++] = (m >> 7) & 127;
            o[n++] = (m >> 14) & 127;
        }
        o[n++] = uint8_t(Held(int(Sw::KEY_26)) | Held(int(Sw::KEY_27)) << 1 | Held(int(Sw::KEY_28)) << 2);
        return n;
    }

    // One channel: its settings and its loop's notes.
    int DescribeChannel(int c, uint8_t* o) const {
        const Channel& ch = ch_[c];
        int n = 0;
        o[n++] = uint8_t(c);
        o[n++] = uint8_t(ch.preset);
        o[n++] = uint8_t(ch.octave + 2);
        o[n++] = uint8_t(ch.ratio);
        o[n++] = uint8_t(pending_[c] + 1);
        o[n++] = Q7(ch.level);
        o[n++] = Q7(ch.pan);
        for (float v : {ch.p.strike, ch.p.brightness, ch.p.decay, ch.p.shimmer, ch.p.variation, ch.p.buzz})
            o[n++] = Q7(v);
        const Loop& lp = loop_[c];
        o[n++] = uint8_t(lp.count);
        for (int i = 0; i < lp.count; ++i) {
            const int ph = std::min(int(lp.n[i].phase * 16384.f), 16383);
            o[n++] = ph & 127;
            o[n++] = ph >> 7;
            o[n++] = uint8_t(std::min(std::max(lp.n[i].note, 0), 127));
        }
        return n;
    }
    static constexpr int kDescribeMax = 14 + 3 * kLoopNotes;

    // ---- LEDs (main loop) ------------------------------------------------------
    void DrawLeds(Hardware& hw, float dt) {
        using namespace chompi;
        const bool chompi = hw.button_sr.State(int(Sw::KEY_26));
        const float* sel_col = kPresetColor[ch_[sel_].preset];
        // The controls the page points at breathe in white over what they show.
        const float hint_glow = .35f - .35f * std::cos(led_time_ * 2.f * float(M_PI) / 1.2f);
        hint_glow_ = hint_glow;

        for (int k = 0; k < 25; ++k) {
            float rgb[3] = {0.f, 0.f, 0.f};
            if (reboot_armed_) {
                rgb[0] = rgb[1] = rgb[2] = 1.f;
            } else if (meter_wait_ > 0) {
                // The meter, with the selected channel's notes over it in its colour.
                static constexpr float kWhite[3] = {1.f, 1.f, 1.f};
                MeterKey(k, meter_white_ ? kWhite : sel_col, rgb);
                for (int j = 0; j < 3; ++j) rgb[j] = std::max(rgb[j], sel_col[j] * key_flash_[k]);
            } else if (show_load_) {
                // A bar of active voices from the lowest key; green, yellow, red by load.
                if (k < ActiveVoices()) {
                    const float* col = load_ < .6f ? kGreen : load_ < .85f ? kYellow : kRed;
                    std::copy(col, col + 3, rgb);
                }
            } else if (sound_view_ && kKeys[k].white) {
                // The sounds on the first white keys, in their colours; the channel's bright.
                const int s = WhiteIndex(k);
                if (s < kNumMalletPresets) {
                    const float b = s == ch_[sel_].preset ? 1.f : .1f;
                    for (int j = 0; j < 3; ++j) rgb[j] = kPresetColor[s][j] * b;
                }
            } else if ((play_down_ || chompi) && kKeys[k].white) {
                // The channels: each white key in its channel's colour, flashing on
                // its notes. CHOMPI: the selected one bright; play: the ones
                // listened to bright. Muted channels blink red.
                const int c = WhiteIndex(k);
                if (muted_[c]) {
                    rgb[0] = std::fmod(led_time_, 1.f) < .5f ? .6f : 0.f;
                } else {
                    const float* col = kPresetColor[ch_[c].preset];
                    const bool lit = play_down_ ? listen_ & (1u << c) : c == sel_;
                    const float b = (lit ? .6f : play_down_ ? .2f : .06f) + .4f * note_flash_[c];
                    for (int j = 0; j < 3; ++j) rgb[j] = col[j] * b;
                }
            } else if (play_down_) {
            } else if (chompi) {
                // The ratios on the black keys: the selected channel's (switch on
                // all: the ensemble's) bright; one waiting for the next beat blinks.
                const int b = BlackIndex(k);
                float v = b == (all_ ? ens_ : ch_[sel_].ratio) ? 1.f : .08f;
                if (b == (all_ ? pending_ens_ : pending_[sel_])) v = std::fmod(led_time_, .25f) < .125f ? 1.f : .08f;
                rgb[0] = rgb[1] = rgb[2] = v;
            } else if (oct_show_ > 0 && OctaveKey(k) >= -2) {
                // The octave on the five lowest black keys, -2 .. +2: the current one bright.
                const float b = OctaveKey(k) == ch_[sel_].octave ? 1.f : .1f;
                rgb[0] = rgb[1] = rgb[2] = b;
            } else {
                for (int j = 0; j < 3; ++j) rgb[j] = sel_col[j] * key_flash_[k];
            }
            if ((hint_ >> k) & 1)
                for (float& x : rgb) x = std::max(x, hint_glow);
            SetSmtLedFloat(kKeys[k].led, rgb[0], rgb[1], rgb[2]);
            key_flash_[k] *= std::exp(-dt / .25f);
        }

        // Knobs: the selected channel's colour; with the switch on all, the
        // colours of all channels, passing along the four knobs a step per L0 beat.
        led_time_ += dt;
        for (int n = 0; n < 4; ++n) {
            const float* kc = sel_col;
            if (all_) kc = kPresetColor[ch_[(l0_beats_ + n) % kChannels].preset];
            Pth(kKnobs[n].led, kc[0], kc[1], kc[2]);
        }
        Pth(kKnobs[kTransport].led, sel_col[0], sel_col[1], sel_col[2]);

        // The wheel's two lights: a beat on every L0 (the tempo lives here).
        const float wb = running_ ? .05f + .95f * l0_flash_ : 0.f;
        Pth(kWheelLeds[0], wb, wb, wb);
        Pth(kWheelLeds[1], wb, wb, wb);
        // K6: as bright as the volume.
        const float vb = .1f + .5f * volume_;
        Pth(kKnobs[kVolume].led, vb, vb, vb);

        // Play: lit while the loops run. Loop: a beat on every loop of the
        // selected channel, brighter when it has a loop.
        Pth(kPlayLed, 0.f, running_ ? .5f : 0.f, 0.f);
        const float lb = (loop_[sel_].count ? .25f : .03f) + (running_ ? .6f * ch_flash_[sel_] : 0.f);
        if (loop_down_ && !loop_used_ && loop_hold_ >= kClearWarnBlocks)
            Pth(kLoopLed, 1.f, 0.f, 0.f); // keep holding to clear
        else
            Pth(kLoopLed, sel_col[0] * lb, sel_col[1] * lb, sel_col[2] * lb);
        fill_led_data();

        const float fade = std::exp(-dt / .15f);
        l0_flash_ *= fade;
        for (float& f : ch_flash_) f *= fade;
        const float note_fade = std::exp(-dt / .1f);
        for (float& f : note_flash_) f *= note_fade;
    }

private:
    struct Slot { MalletVoice v; int ch, note; uint32_t age = 0; };

    // A light at the top, with the page's hint over it: CHOMPI 0, K1-K4 1-4,
    // the wheel 5 and 6, play 7, loop 8, K6 9.
    uint64_t hint_ = 0;
    float hint_glow_ = 0.f;
    void Pth(int led, float r, float g, float b) {
        static constexpr int kIdOfLed[10] = {kIdChompi, 28, 29, 30, 31, 32, 32, 26, 27, 33};
        if ((hint_ >> kIdOfLed[led]) & 1) {
            r = std::max(r, hint_glow_);
            g = std::max(g, hint_glow_);
            b = std::max(b, hint_glow_);
        }
        chompi::SetPthLedFloat(led, r, g, b);
    }

    static uint8_t Q7(float x) { return uint8_t(Clamp01(x) * 127.f + .5f); }
    bool Held(int i) const { return (held_ >> i) & 1; }

    // The shift register switch of a control id below the knobs.
    static int SwitchOf(int id) {
        return id < 25 ? int(kKeys[id].sw) : int(Sw::KEY_26) + (id - kIdChompi);
    }

    // Events for the page, from the audio callback; dropped when it falls behind.
    static constexpr int kEvents = 64;
    uint8_t ev_[kEvents][3];
    volatile int ev_head_ = 0, ev_tail_ = 0;
    void Event(int kind, int id, int value = 0) {
        const int next = (ev_head_ + 1) % kEvents;
        if (next == ev_tail_) return;
        ev_[ev_head_][0] = uint8_t(kind);
        ev_[ev_head_][1] = uint8_t(id);
        ev_[ev_head_][2] = uint8_t(kind == kEvTurn ? std::min(std::max(value + 64, 0), 127) : value & 127);
        __sync_synchronize();
        ev_head_ = next;
    }

    // The page's messages to the instrument (see PROTOCOL.md):
    //   3 channel: c preset octave+2 ratio level pan muted count (phase.lo phase.hi note)*
    //     the sound from its preset, the loop from its start;
    //   4 ensemble: selected tempo.lo tempo.hi (L0 in ms) running; every loop
    //     from its start, the knobs' offsets and the field back to neutral;
    //   5 start: as at power-on.
    static constexpr int kMail = 120;
    uint8_t mail_[kMail];
    int mail_len_ = 0;
    volatile bool mail_ready_ = false;
    void Apply(const uint8_t* d, int n) {
        if (n < 1) return;
        if (d[0] == 3 && n >= 9) {
            const int c = d[1];
            if (c >= kChannels) return;
            Channel& ch = ch_[c];
            ch.preset = std::min(int(d[2]), kNumMalletPresets - 1);
            ch.p = kMalletPresets[ch.preset].p;
            ch.octave = std::min(std::max(int(d[3]) - 2, -2), 2);
            ch.ratio = std::min(int(d[4]), kNumRatios - 1);
            ch.level = d[5] / 127.f;
            ch.pan = d[6] / 127.f;
            muted_[c] = d[7];
            pending_[c] = -1;
            Loop& lp = loop_[c];
            lp.count = 0;
            for (int i = 0; i < d[8] && 9 + 3 * i + 2 < n && lp.count < kLoopNotes; ++i) {
                const uint8_t* e = d + 9 + 3 * i;
                lp.n[lp.count++] = {(e[0] | e[1] << 7) / 16384.f, e[2]};
            }
            lp.pos = 0.f;
            hist_[c].since = now_;
            swell_[c] = 1.f;
            Update(c);
        } else if (d[0] == 4 && n >= 5) {
            sel_ = std::min(int(d[1]), kChannels - 1);
            const float ms = float(d[2] | d[3] << 7);
            if (ms > 0.f) l0_ = std::min(std::max(ms * .001f, kMinL0), kMaxL0) * sr_;
            running_ = d[4];
            ens_ = kRatioOne;
            pending_ens_ = -1;
            off_bright_ = off_decay_ = off_strike_ = off_shimmer_ = off_buzz_ = off_variation_ = 0.f;
            width_ = 1.f;
            turn_ = 0.f;
            UpdateAll();
            Restart();
        } else if (d[0] == 5) {
            Start();
        }
    }

    // One key of the meter, in white, behind the notes: the 25 keys in keyboard
    // order are the range (a faint scale), the part up to the value soft, the
    // value itself one bright point (shared by two keys between them, so every
    // click moves it). A bar starts at the left, a centre bar at the middle C.
    // The mark, in the channel's colour: where the value started.
    void MeterKey(int k, const float* mark_col, float* rgb) const {
        const float pos = meter_kind_ == kMeterCentre ? 12.f + meter_v_ * 12.f : meter_v_ * 24.f;
        const float from = meter_kind_ == kMeterCentre ? 12.f : 0.f;
        const bool filled = (k >= std::min(from, pos) - .01f) && (k <= std::max(from, pos) + .01f);
        const float point = Clamp01(1.f - std::fabs(k - pos));
        const float a = std::min(1.f, float(meter_wait_) / kMeterFadeBlocks); // fades out at the end
        const float w = a * (.02f + (filled ? .1f : 0.f) + .6f * point);
        rgb[0] = rgb[1] = rgb[2] = w;
        if (meter_mark_ >= 0.f && k == int(meter_mark_ * 24.f + .5f) && point < .5f)
            for (int j = 0; j < 3; ++j) rgb[j] = std::max(rgb[j], a * .3f * mark_col[j]);
    }

    static constexpr int kClicksPerOctave = 4;
    static constexpr int kOctaveIdleBlocks = 1000; // clicks count towards a step for 1 s
    static constexpr int kOctaveShowBlocks = 1000; // the octave stays on the keys 1 s
    static constexpr float kListenOthers = .25f;   // the others while listening: -12 dB

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

    static constexpr int kClearBlocks = 2000;     // loop held 2 s: clear
    static constexpr int kClearWarnBlocks = 1000; // ... the light red from 1 s


    static constexpr float kMinL0 = .5f, kMaxL0 = 8.f; // seconds
    static constexpr float kTempoStep = 1.01f;         // the wheel: 1% per click
    static constexpr float kFeelSeconds = .03f;        // feel 1: repeats up to 30 ms off

    static int BlackIndex(int k) {
        int b = 0;
        for (int i = 0; i < k; ++i) b += !kKeys[i].white;
        return b;
    }

    // On the beat: the ensemble's new ratio (every loop keeps its place, all
    // lengths scale together), and each channel's new ratio, its loop from the start.
    void ApplyPending() {
        if (pending_ens_ >= 0) ens_ = pending_ens_;
        pending_ens_ = -1;
        for (int c = 0; c < kChannels; ++c) {
            if (pending_[c] < 0) continue;
            ch_[c].ratio = pending_[c];
            pending_[c] = -1;
            loop_[c].pos = 0.f;
            for (int i = 0; i < loop_[c].count; ++i) {
                loop_[c].n[i].armed = true;
                loop_[c].n[i].jit = 0.f;
            }
        }
    }

    // The beat: L0 times the ensemble's ratio. A channel's loop: the beat times its ratio.
    float Beat() const { return l0_ * kRatios[ens_]; }
    float LoopSamples(int c) const { return Beat() * kRatios[ch_[c].ratio]; }

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
            // Kept without the channel's octave: the loop follows the octave, as it
            // follows the other settings.
            const int note = e.note - 12 * ch_[c].octave;
            if (n < kLoopNotes) got[n++] = {ph, note};
            else { std::copy(got + 1, got + kLoopNotes, got); got[kLoopNotes - 1] = {ph, note}; }
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
        auto crossed = [&](float x) {
            x -= std::floor(x);
            return wrap ? (x >= from || x < to) : (x >= from && x < to);
        };

        // The feel (variation): a player, not a machine. Each repeat lands a
        // little early or late, a little louder or softer, the channel swells
        // and ebbs over the loops, and a loose player now and then leaves one out.
        const float feel = Effective(c).variation;
        if (wrap) swell_[c] += (1.f - feel * .5f * Rand() - swell_[c]) * .35f;
        const float jit_max = feel * kFeelSeconds * sr_ / LoopSamples(c);
        const float skip = std::max(feel - .5f, 0.f) * .3f;

        for (int i = 0; i < lp.count; ++i) {
            LoopNote& n = lp.n[i];
            if (!n.armed) {
                if (crossed(n.phase + .5f)) n.armed = true; // half a loop later: ready again
                continue;
            }
            if (!crossed(n.phase + n.jit)) continue;
            n.armed = false;
            n.jit = jit_max * (Rand() + Rand() - 1.f);
            if (Rand() < skip) continue;
            NoteOn(c, n.note + 12 * ch_[c].octave, swell_[c] * (1.f - feel * .35f * Rand()));
            if (c == sel_) FlashNote(n.note);
        }
    }

    float Rand() {
        rng_ = rng_ * 1664525u + 1013904223u;
        return float(rng_ >> 8) * (1.f / 16777216.f);
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

    // The channel of a white key held from before this block (not key k), or -1.
    int HeldWhite(int k) const {
        const uint64_t before = held_ & ~pressed_;
        for (int i = 0; i < 25; ++i)
            if (i != k && kKeys[i].white && ((before >> int(kKeys[i].sw)) & 1)) return WhiteIndex(i);
        return -1;
    }

    // Channel `to` takes over the pattern of `from`: its loop and ratio, in step
    // with it. Everything else stays: `to` plays the figure on its own
    // instrument, in its own octave, with its own level, pan and swell.
    void Copy(int from, int to) {
        if (from == to) return;
        loop_[to] = loop_[from];
        ch_[to].ratio = ch_[from].ratio;
        pending_[to] = pending_[from];
        hist_[to].since = now_; // older notes played on `to` are not looped any more
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
        p.shimmer = Clamp01(p.shimmer + off_shimmer_);
        p.buzz = Clamp01(p.buzz + off_buzz_);
        p.variation = Clamp01(p.variation + off_variation_);
        return p;
    }

    void NoteOn(int c, int note, float velocity = 1.f) {
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
        s.v.Strike(MidiToHz(float(note)), Effective(c), velocity);
    }

    void Update(int c) { dirty_[c] = true; }
    void UpdateAll() { for (int c = 0; c < kChannels; ++c) dirty_[c] = true; }

    void UpdateVoices(int c) {
        const MalletParams p = Effective(c);
        for (auto& s : slot_)
            if (s.v.Active() && s.ch == c) s.v.SetParams(p);
    }

    // Knobs 1-4, no pages: turn | CHOMPI + turn. (The sound: CHOMPI + the
    // channel's key tapped twice; the octave: the wheel.)
    //   K1 strike     | shimmer
    //   K2 brightness | feel (variation)
    //   K3 decay      | buzz
    //   K4 level      | pan
    // With the switch on all: K1-K3 as offsets on every channel, so each keeps
    // its character; K4 the stereo field: width | turn.
    // Every turn shows its value on the keys (the meter).
    void Turn(int knob, int inc, bool chompi) {
        const float d = inc * .02f;
        if (all_) {
            if (knob == 3) {
                if (chompi) {
                    turn_ = std::min(std::max(turn_ + d, -.5f), .5f);
                    ShowMeter(kMeterCentre, turn_ * 2.f, -1.f, true);
                } else {
                    width_ = std::min(std::max(width_ + 2.f * d, 0.f), 2.f);
                    ShowMeter(kMeterBar, width_ * .5f, .5f, true); // the mark: as set
                }
                return;
            }
            float& off = knob == 0 ? (chompi ? off_shimmer_ : off_strike_)
                       : knob == 1 ? (chompi ? off_variation_ : off_bright_)
                                   : (chompi ? off_buzz_ : off_decay_);
            Offset(off, d);
            ShowMeter(kMeterCentre, off * 2.f, -1.f, true);
            UpdateAll();
            return;
        }
        Channel& c = ch_[sel_];
        MalletParams& p = c.p;
        const MalletParams& orig = kMalletPresets[c.preset].p;
        if (knob == 3) {
            if (chompi) {
                c.pan = Clamp01(c.pan + d);
                ShowMeter(kMeterCentre, (c.pan - .5f) * 2.f);
            } else {
                c.level = Clamp01(c.level + d);
                ShowMeter(kMeterBar, c.level, Channel().level);
            }
            return;
        }
        float& v = knob == 0 ? (chompi ? p.shimmer : p.strike)
                 : knob == 1 ? (chompi ? p.variation : p.brightness)
                             : (chompi ? p.buzz : p.decay);
        const float o = knob == 0 ? (chompi ? orig.shimmer : orig.strike)
                      : knob == 1 ? (chompi ? orig.variation : orig.brightness)
                                  : (chompi ? orig.buzz : orig.decay);
        v = Clamp01(v + d);
        ShowMeter(kMeterBar, v, o); // the mark: where the sound started
        Update(sel_);
    }

    static void Offset(float& off, float d) { off = std::min(std::max(off + d, -.5f), .5f); }

    // Clicks that only count every few: a step (+1 or -1) once enough have
    // gathered on the same thing (id) within a second, else 0.
    int Steps(int inc, int id, int clicks = kClicksPerOctave) {
        if (step_idle_ == 0 || step_id_ != id) step_clicks_ = 0;
        step_id_ = id;
        step_idle_ = kOctaveIdleBlocks;
        step_clicks_ += inc;
        if (std::abs(step_clicks_) < clicks) return 0;
        const int step = step_clicks_ > 0 ? 1 : -1;
        step_clicks_ = 0;
        return step;
    }

    static constexpr int kSwitches = 40; // 5 shift registers of 8
    bool Pressed(int i) const { return (pressed_ >> i) & 1; }
    uint64_t held_ = 0, pressed_ = 0;

    float sr_ = 48000.f;
    float preset_gain_[kNumMalletPresets] = {};
    Channel ch_[kChannels];
    Slot slot_[kVoices];
    bool dirty_[kChannels] = {};
    uint32_t clock_ = 0;
    int sel_ = 0;
    float led_time_ = 0.f;
    float swell_[kChannels] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    uint32_t rng_ = 12345;
    bool all_ = false, show_load_ = false;
    float off_bright_ = 0.f, off_decay_ = 0.f;
    float off_strike_ = 0.f, off_shimmer_ = 0.f, off_buzz_ = 0.f, off_variation_ = 0.f;
    float room_ = .5f, volume_ = .8f, load_ = 0.f;
    float width_ = 1.f, turn_ = 0.f; // the ensemble's stereo field

    // The meter: while a knob turns (and a moment after), the keys show its value.
    enum MeterKind { kMeterBar, kMeterCentre };
    static constexpr int kMeterBlocks = 2000;     // 1.5 s after the last click, then
    static constexpr float kMeterFadeBlocks = 500.f; // half a second to fade out
    int meter_wait_ = 0, meter_kind_ = kMeterBar;
    float meter_v_ = 0.f, meter_mark_ = -1.f; // 0..1 (centre: -1..1); mark < 0: none
    bool meter_white_ = false; // the ensemble's and the instrument's: a white mark, else the channel's colour
    void ShowMeter(int kind, float v, float mark = -1.f, bool white = false) {
        meter_kind_ = kind;
        meter_v_ = v;
        meter_mark_ = mark;
        meter_white_ = white;
        meter_wait_ = kMeterBlocks;
    }
    float key_flash_[25] = {}, note_flash_[kChannels] = {};
    int step_clicks_ = 0, step_id_ = 0, step_idle_ = 0, oct_show_ = 0;
    int reboot_hold_ = 0;

    Loop loop_[kChannels];
    History hist_[kChannels];
    uint32_t now_ = 0;      // samples since start
    float l0_ = 96000.f;    // L0 in samples (2 s)
    float l0_pos_ = 0.f, l0_flash_ = 0.f, ch_flash_[kChannels] = {};
    uint32_t l0_beats_ = 0; // L0 beats so far
    bool running_ = true;
    bool loop_down_ = false, loop_used_ = false, loop_chompi_ = false;
    bool play_down_ = false, play_used_ = false;
    bool muted_[kChannels] = {};
    uint32_t listen_ = 0;     // channels listened to (play + white key held), a bit each
    bool sound_view_ = false; // the selected channel's sound on the white keys
    int tap_key_ = -1;        // the white key last tapped with CHOMPI held
    bool tap_again_ = false;  // ... and it was tapped before: the sounds on letting go
    float gate_[kChannels] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    int ens_ = kRatioOne, pending_ens_ = -1;
    int pending_[kChannels] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    int loop_hold_ = 0, loop_ch_ = 0;
    bool reboot_armed_ = false, reboot_now_ = false;
};

} // namespace meter
