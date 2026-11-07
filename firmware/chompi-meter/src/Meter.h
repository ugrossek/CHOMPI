#pragma once
// The meter instrument: 15 channels, each a mallet sound; the keys play the
// selected channel. Loops are recorded afterwards: loop turns what you just
// played on a channel (its last loop length) into that channel's loop.
//
// Everything here runs in the audio callback (controls once per block, then
// the sound), except DrawLeds(), which the main loop calls.

#include "../dsp/Mallet.h"
#include "../dsp/Pan.h"
#include "../dsp/Presets.h"
#include "Voice.h"
#include "board/hardware.h"
#include "board/temp_led_stuff.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace meter {

// The release, or (make TEST=1) with what is still being tried: a figure's
// versions (CHOMPI + loop held + wheel) and choosing its length after playing (loop
// held + a black key). The versions are saved either way.
#ifndef CIRCLES_TEST
#define CIRCLES_TEST 0
#endif
static constexpr bool kTestBuild = CIRCLES_TEST;
// Sliding a figure (loop held + wheel; loop held + push: back where it was kept)
// comes in beta 2: make SLIDE=1, and in the test build.
#ifndef CIRCLES_SLIDE
#define CIRCLES_SLIDE 0
#endif
static constexpr bool kSlide = CIRCLES_SLIDE;
// Drift (make DRIFT=1, with the slide): every player runs at its own speed,
// pulled towards its ghost, where it would be had it stayed in step with L0:
//   speed = 1 + detune + pull * sin 2pi (ghost - where it is), kept to 0.5 .. 1.5.
// All of it on CHOMPI + play held, which neither starts nor stops: the keys show
// where the chosen player is against its place, live; + wheel turned: its detune
// (on all: the pull), + wheel pushed and turned: a kick, + wheel pushed and let
// go without turning: exact and home again (on all: everyone). Play and loop on
// their own are what they are without drift. A new length comes in on the next
// L0 beat, the player back on its place there (Align To L0 false in
// /CIRCLES/options.json: at the player's own loop start, so nothing jumps but
// what a hand does).
#ifndef CIRCLES_DRIFT
#define CIRCLES_DRIFT 0
#endif
static constexpr bool kDrift = CIRCLES_DRIFT;
// The figure library (make LIBRARY=1, with drift): every figure kept goes into
// one list, in the order it was kept, and any player can take any of them.
// CHOMPI + loop held + wheel moves the chosen player on to the next figure
// (right) or back to the one before (left); on all, every player that has a
// figure. The new figure comes in on the next L0 beat, in step with L0 as it
// was kept (or, Align To L0 false in /CIRCLES/options.json, at the player's own
// loop start), in its own sound and octave; while turning, the keys show its notes.
#ifndef CIRCLES_LIBRARY
#define CIRCLES_LIBRARY 0
#endif
static constexpr bool kLibrary = CIRCLES_LIBRARY;

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
// apart as the LEDs allow, then the new ones. The fourteen on the white keys
// all differ, and no two neighbours are near (green, lime and sea green, or
// white and warm white, stand apart).
static const float kPresetColor[kNumMalletPresets][3] = {
    {1.0f, .35f, .00f}, // wood: orange
    {1.0f, .00f, .00f}, // marimba: red
    {.00f, .00f, 1.0f}, // vibes: blue
    {1.0f, .85f, .00f}, // bronze: yellow
    {.00f, .45f, 1.0f}, // glass: sky (cyan went to the hang)
    {1.0f, 1.0f, 1.0f}, // bell: white
    {.45f, .00f, 1.0f}, // gong: purple
    {1.0f, .00f, .60f}, // felt: pink
    {.00f, 1.0f, .00f}, // snare bar: green
    {1.0f, .60f, .25f}, // muffled: warm white
    {1.0f, .00f, 1.0f}, // amadinda: magenta
    {1.0f, .85f, .00f}, // gangsa: yellow, as bronze
    {.00f, 1.0f, .45f}, // mbira: sea green
    {1.0f, .00f, .60f}, // kora: pink, as felt
    {.00f, 1.0f, 1.0f}, // hang: cyan
    {.55f, 1.0f, .00f}, // singing bowl: lime
    {1.0f, .00f, .60f}, // bowed glass: pink, as felt
    {.00f, 1.0f, .00f}, // voices: green, as the snare bar
    {.00f, 1.0f, .45f}, // siku: sea green
    {.00f, .45f, 1.0f}, // pulse piano: sky, as glass
    {1.0f, .00f, 1.0f}, // hands: magenta
    {1.0f, .60f, .25f}, // tape: warm white
    // The second set: the same fourteen hues in another order (a kick and a
    // marimba look alike on a player's key; the instrument view tells them apart).
    {1.0f, .00f, .00f}, // kick: red
    {1.0f, .35f, .00f}, // tom: orange
    {1.0f, 1.0f, 1.0f}, // snare: white
    {1.0f, .00f, .60f}, // clap: pink
    {1.0f, .85f, .00f}, // hi-hat: yellow
    {.00f, 1.0f, .45f}, // cowbell: sea green
    {1.0f, .00f, 1.0f}, // blip: magenta
    {.55f, 1.0f, .00f}, // acid: lime
    {1.0f, .60f, .25f}, // FM piano: warm white
    {.00f, .45f, 1.0f}, // FM bell: sky
    {.00f, 1.0f, .00f}, // chip lead: green
    {.45f, .00f, 1.0f}, // saw stab: purple
    {.00f, 1.0f, 1.0f}, // grit: cyan
    {.00f, .00f, 1.0f}, // FM bass: blue
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
    float level = .8f;
    float pan = 0.f; // -1 left .. 1 right
    int octave = 0;
    int last_note = 60; // the last key played, before the octave: the preview note
    int ratio = kRatioOne; // index into kRatios: the loop length is L0 times this
    float slide = 0.f; // how far loop held + wheel has slid the figure, in beats (for the keys)
};

// A channel's loop: each note at its time from the loop's start, in beats (L0
// times the ensemble's ratio), so a new tempo carries the notes along. The
// channel's ratio is the loop's length: a longer loop adds rest after the
// notes, a shorter one wraps the notes past its end round to its beginning
// (they play over the others; when the loop grows again they unfold back to
// where they were). Nothing is cut and the notes are never stretched.
// A kept loop begins where its window began, one length before loop was
// pressed. seam: where that beginning fell after an L0 beat, in beats, so the
// loop keeps its place against L0 when everything starts again.
// at: the time in beats; note: without the channel's octave, which is added when
// it plays (the loop follows the octave); phase: its place in the loop as it is
// now (at / ratio, wrapped); silent: not played (unused since the wrap); jit:
// this repeat lands a little early or late (the feel); armed: not yet played in
// this round.
struct LoopNote { float at; int note; float phase = 0.f; bool silent = false; float jit = 0.f; bool armed = true; float slid = 0.f; };
// (slid: how far loop held + wheel has moved it, in beats, so it can go back exactly.)
constexpr int kLoopNotes = 32;
struct Loop {
    LoopNote n[kLoopNotes];
    int count = 0;
    float pos = 0.f;  // where the channel is in its loop, 0..1
    float seam = 0.f; // where the loop starts after an L0 beat, in beats (0..1)
};

// What was played by hand on a channel, newest last; loop takes from here.
struct Played { uint32_t t; int note; };
constexpr int kHistory = 48;
struct History {
    Played e[kHistory];
    int head = 0, count = 0;
    uint32_t since = 0; // only notes after the last loop press (or clear) count
};

// The versions of the figures, so nothing is lost: before a channel's figure
// changes (kept, added to, cleared, copied over, a new length, set by the page)
// it is saved, the last kVersions of each channel; before everyone's change at
// once (clear all, a setup from the page), the whole ensemble, the last
// kEnsembleVersions. Loop held + the wheel steps through them (with CHOMPI: the
// ensemble's). A version is the notes and the length, and where its loop was
// when it was saved: it comes back where it would be had it gone on playing, so
// its place against the other players is the one it had.
// Plain data in SDRAM, set up in Init (no constructors run there).
struct Figure { LoopNote n[kLoopNotes]; int count; float seam; int ratio; float pos; uint32_t t; };
constexpr int kVersions = 8, kEnsembleVersions = 4;
// The library: the last kLibrarySize figures kept. Figure number s (counted from
// the first ever kept) sits at lib[s % kLibrarySize]; the oldest go when it is full.
constexpr int kLibrarySize = 128;
// A ring of saved versions: head the oldest, count how many; cur the one
// playing while stepping (-1: the figure as it is, not stepped to).
struct Ring { int head, count, cur; };
struct Versions {
    Figure f[kChannels][kVersions];
    Ring ring[kChannels];
    Figure e[kEnsembleVersions][kChannels];
    Ring ering;
    Figure lib[kLibrarySize];
    int lib_next; // how many figures were ever put in
};

class Instrument {
public:
    // voice_mem: AnyVoice::kArenaFloats for each voice, the study voices' buffers.
    void Init(float sample_rate, Versions* versions, float* voice_mem) {
        sr_ = sample_rate;
        ver_ = versions;
        for (Ring& r : ver_->ring) r = {0, 0, -1};
        ver_->ering = {0, 0, -1};
        ver_->lib_next = 0;
        Start();
        for (int i = 0; i < kNumMalletPresets; ++i)
            preset_gain_[i] = std::pow(10.f, kMalletPresets[i].gain_db / 20.f);
        for (int v = 0; v < kVoices; ++v) {
            slot_[v].v.Init(sample_rate, v + 1, voice_mem + v * AnyVoice::kArenaFloats);
            slot_[v].ch = slot_[v].note = -1;
        }
        PlaceAll(true);
    }

    // As at power-on: every channel on its own sound, no loops, everything at
    // its start. (The volume and the room stay as they are.)
    void Start() {
        l0_ = kStartL0 * sr_;
        for (int c = 0; c < kChannels; ++c) {
            ch_[c] = Channel();
            // Players 1-14 on the instruments of set 1 in key order, 15 on the first again.
            ch_[c].preset = kSetPresets[0][c % 14];
            ch_[c].p = kMalletPresets[ch_[c].preset].p;
            ch_[c].pan = StartPan(c); // fanned out from the middle
            ch_[c].octave = ch_[c].preset == 6 ? -1 : 0;  // the gong sounds best low
            loop_[c].count = 0;
            loop_[c].seam = 0.f;
            hist_[c].since = now_;
            pending_[c] = -1;
            muted_[c] = false;
            swell_[c] = 1.f;
            dirty_[c] = true;
            detune_[c] = 0.f;
            glide_[c] = false;
            lib_at_[c] = lib_next_[c] = -1; // (the library itself stays)
        }
        pull_ = kStartPull;
        sel_ = 0;
        running_ = true;
        ens_ = kRatioOne;
        pending_ens_ = -1;
        size_ = 1.f;
        gain_ = 1.f;
        PlaceAll(true);
        Restart();
    }

    // Everything from a new L0 beat; every loop where its seam puts it (a loop
    // played right before the beat starts a little before it).
    void Restart() {
        l0_pos_ = 0.f;
        for (int c = 0; c < kChannels; ++c) {
            const float p = -loop_[c].seam / Len(c);
            loop_[c].pos = p - std::floor(p);
            Anchor(c);
            for (int i = 0; i < loop_[c].count; ++i) {
                loop_[c].n[i].armed = true;
                loop_[c].n[i].jit = 0.f;
            }
        }
    }

    // ---- Controls (audio callback, once per block) --------------------------
    void Controls(Hardware& hw, size_t block) {
        auto& sr = hw.button_sr;
        // The switches, once per block: down now (held_), down now and up last
        // block (pressed_; the shift register's own RisingEdge only re-arms after
        // FallingEdge is asked). More down at once than two hands can hold is a
        // misread (every key at once: with play among them, a tap on every
        // player, which muted them all); that block is left out, so it presses
        // nothing.
        uint64_t down = 0;
        for (int i = 0; i < kSwitches; ++i)
            if (sr.State(i)) down |= uint64_t(1) << i;
        if (__builtin_popcountll(down) > kMostDown) {
            if (__builtin_popcountll(held_) <= kMostDown) misreads_++;
            down = held_;
        }
        pressed_ = down & ~held_;
        const uint64_t released = held_ & ~down;
        held_ = down;
        const bool chompi = Down(int(Sw::KEY_26));
        const bool all = !hw.GetToggleState();
        if (all != all_) Event(kEvSwitch, kIdSwitch, all);
        all_ = all;

        // A message from the page, waiting since the last block.
        if (mail_ready_) {
            Apply(mail_, mail_len_);
            __sync_synchronize();
            mail_ready_ = false;
        }

        for (int id = 0; id < kIdKnob; ++id) {
            const int i = SwitchOf(id);
            if ((pressed_ >> i) & 1) Event(kEvPress, id);
            if ((released >> i) & 1) Event(kEvRelease, id);
        }

        // Keys: CHOMPI + white key selects a channel (tapped twice: its sound on
        // the white keys); play + a tap on a white key mutes that channel (or
        // unmutes it), play + the white key held listens to it; otherwise the
        // key plays.
        const bool play_held = Down(int(Sw::KEY_27)), loop_held = Down(int(Sw::KEY_28));
        if (!chompi) { sound_view_ = false; tap_key_ = -1; }
        // CHOMPI pressed: the keys show the players at once, not the last knob's value.
        if (Pressed(int(Sw::KEY_26))) meter_wait_ = 0;
        // Let go after choosing a player: its key glows on a moment, so players
        // with the same colour (1 and 11 ...) can be told apart.
        if ((released >> int(Sw::KEY_26)) & 1) {
            if (chose_) sel_glow_ = kSelGlowSeconds;
            chose_ = false;
        }
        for (int k = 0; k < 25; ++k) {
            const int i = int(kKeys[k].sw);
            if (Pressed(i)) {
                key_flash_[k] = 1.f;
                if (kDrift && chompi && play_held && kKeys[k].white) {
                    // Looking at the drift: another player to look at.
                    sel_ = WhiteIndex(k);
                    chose_ = true;
                } else if (play_held) {
                    if (kKeys[k].white) white_down_[WhiteIndex(k)] = 0; // tap or hold: see below
                    play_used_ = true;
                } else if (sound_view_ && kKeys[k].white) {
                    // The instruments of a set on the white keys: pick one, heard at once.
                    const int preset = kSetPresets[view_set_][WhiteIndex(k)];
                    if (preset >= 0) {
                        Channel& c = ch_[sel_];
                        c.preset = preset;
                        c.p = kMalletPresets[c.preset].p;
                        Update(sel_);
                        NoteOn(sel_, c.last_note + 12 * c.octave);
                    }
                } else if (sound_view_) {
                    // The sets on the black keys, from the lowest (with more than one).
                    if (kSets > 1 && BlackIndex(k) < kSets) view_set_ = BlackIndex(k);
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
                    chose_ = true;
                } else if (chompi) {
                    // CHOMPI + black key: the selected channel's ratio, from the next
                    // L0 beat (its loop goes on from where it is, see ApplyPending).
                    // With the switch on all: the ensemble's ratio, a
                    // multiplier on every channel's length (1: as recorded).
                    if (all_) pending_ens_ = BlackIndex(k);
                    else pending_[sel_] = BlackIndex(k);
                    if (!running_) ApplyPending(); // stopped: no beat to wait for
                } else if (kTestBuild && loop_down_ && !loop_chompi_ && !kKeys[k].white) {
                    // Loop held + a black key: what was played before loop went
                    // down, kept again with this length. Another one: again.
                    Rekeep(loop_ch_, BlackIndex(k));
                } else {
                    Channel& c = ch_[sel_];
                    c.last_note = kKeys[k].note;
                    NoteOn(sel_, kKeys[k].note + 12 * c.octave);
                    Remember(sel_, kKeys[k].note + 12 * c.octave);
                }
            }
        }

        if (tap_again_ && tap_key_ >= 0 && !Down(int(kKeys[tap_key_].sw))) {
            sound_view_ = true;
            view_set_ = SetOf(ch_[sel_].preset, view_set_);
            tap_again_ = false;
        }

        // Knobs 1-4. CHOMPI + push: back to where the sound started, both of
        // the knob's layers (as on TAPE, where it resets all of a knob's pages).
        for (int n = 0; n < 4; ++n) {
            const int inc = hw.enc[kKnobs[n].enc].Increment();
            if (inc) { Turn(n, inc, chompi); Event(kEvTurn, kIdKnob + n, inc); }
            if (chompi && Pressed(kKnobs[n].push)) Reset(n);
        }
        // Wheel: the selected channel's octave, a step every few clicks, shown on
        // the black keys (the keys stay free to try it). CHOMPI + turn: the
        // tempo, L0, smoothly (right is faster); every loop follows. Loop held +
        // turn: the selected channel's figure slides in its loop, a 64th of its
        // length per click, nearly smooth (right later, left earlier), shown
        // across the keys; loop held + push the wheel: back to where it was kept.
        // (Test build: CHOMPI + loop held + turn: the versions of its figure,
        // left older, right newer, shown on the black keys.)
        // CHOMPI + push the wheel: the tempo back to where it is at power-on.
        // (Its switch is on the encoder itself, not in the shift registers.)
        // (Drift, all with CHOMPI + play held, the keys showing where the
        // selected player is against its place, live: + turn detunes it, 0.1%
        // a click up to 5% either way, right faster, shown as a centre bar for
        // a moment; + push and turn: a kick, a 64th of its length per click,
        // and the pull brings it back; + push and let go: exact again and it
        // glides home. On all: + turn is the pull, + push and let go: everyone
        // exact and home. Play or loop held + turn: nothing.)
        // Drift: CHOMPI + play held is looking at the chosen player's drift (and
        // changing it); play does not stop when let go.
        const bool look = kDrift && chompi && play_held;
        if (look) play_used_ = true;
        if (hw.enc[kKnobs[kTransport].enc].RisingEdge()) wheel_turned_ = false;
        if (look && hw.enc[kKnobs[kTransport].enc].FallingEdge() && !wheel_turned_) {
            // Pushed and let go without turning: exact and home again.
            for (int c = 0; c < kChannels; ++c)
                if (all_ || c == sel_) { detune_[c] = 0.f; glide_[c] = true; }
            ShowPhase();
        }
        if (hw.enc[kKnobs[kTransport].enc].RisingEdge() && !look) {
            if (chompi) l0_ = kStartL0 * sr_;
            else if (kSlide && !kDrift && loop_down_) { // loop held + push: the figure back where it was kept (on all: everyone's)
                if (!shift_saved_) { if (all_) SaveAll(); else Save(sel_); shift_saved_ = true; }
                for (int c = 0; c < kChannels; ++c)
                    if (all_ || c == sel_) Back(c);
                ShowMeter(kMeterBar, 0.f, -1.f, true);
                shift_show_ = true;
                loop_used_ = true;
            }
        }
        if (const int inc = hw.enc[kKnobs[kTransport].enc].Increment()) {
            Event(kEvTurn, kIdKnob + kTransport, inc);
            if (look) {
                const bool pushed = hw.enc[kKnobs[kTransport].enc].Pressed();
                if (pushed) wheel_turned_ = true;
                if (pushed && !all_) {
                    Kick(sel_, inc);
                    ShowPhase();
                } else if (pushed) {
                    // (On all, a kick for everyone together is not heard: nothing.)
                } else if (all_) {
                    pull_ = std::min(std::max(pull_ + inc * kPullStep, 0.f), kMaxPull);
                    ShowMeter(kMeterBar, pull_ / kMaxPull, kStartPull / kMaxPull, true);
                } else {
                    detune_[sel_] = std::min(std::max(detune_[sel_] + inc * kDetuneStep, -kMaxDetune), kMaxDetune);
                    ShowMeter(kMeterCentre, detune_[sel_] / kMaxDetune);
                }
            } else if (kLibrary && loop_down_ && chompi) {
                // CHOMPI + loop held + turn: the chosen player (on all: everyone
                // with a figure) on to the next figure in the library, or back.
                if (const int step = Steps(inc, kStepLibrary, kClicksPerVersion)) {
                    bool moved = false;
                    lib_show_fig_ = -1;
                    for (int c = 0; c < kChannels; ++c)
                        if (all_ ? loop_[c].count > 0 : c == sel_) moved |= MoveOn(c, step);
                    if (!moved) nokeep_ = kNoKeepSeconds; // nowhere to go: two dim blinks
                }
                loop_used_ = true;
            } else if (kDrift && (play_held || loop_down_)) {
                // (Drift: play or loop held + wheel do nothing; see CHOMPI + play.)
                if (loop_down_) loop_used_ = true;
                if (play_held) play_used_ = true; // (a turn is not a tap: play does not stop)
            } else if (kSlide && loop_down_ && !(kTestBuild && chompi)) {
                // (On all, nothing: everyone sliding together is not heard.)
                if (!all_) {
                    if (!shift_saved_) { Save(sel_); shift_saved_ = true; }
                    Slide(sel_, inc);
                    const float f = ch_[sel_].slide / Len(sel_);
                    ShowMeter(kMeterBar, f - std::floor(f), -1.f, true);
                }
                shift_show_ = true;
                loop_used_ = true; // (not a tap that kept nothing)
            } else if (kTestBuild && loop_down_) {
                if (const int step = Steps(inc, kStepVersions, kClicksPerVersion)) Browse(sel_, step);
                vshow_ = true;
                vshow_all_ = false;
                loop_used_ = true;
            } else if (chompi) {
                l0_ = std::min(std::max(l0_ * std::pow(kTempoStep, float(-inc)), kMinL0 * sr_), kMaxL0 * sr_);
            } else {
                if (const int step = Steps(inc, sel_)) ch_[sel_].octave = std::min(std::max(ch_[sel_].octave + step, -2), 2);
                oct_show_ = kOctaveShowBlocks;
            }
        }
        if (step_idle_ > 0) step_idle_--;
        if (oct_show_ > 0) oct_show_--;
        if (meter_wait_ > 0) meter_wait_--;
        if (lib_show_ > 0) lib_show_--;
        // Right knob: volume; CHOMPI + turn: the room.
        if (const int inc = hw.enc[kKnobs[kVolume].enc].Increment()) {
            Event(kEvTurn, kIdKnob + kVolume, inc);
            float& v = chompi ? room_ : volume_;
            v = Clamp01(v + inc * .02f);
            ShowMeter(kMeterBar, v, -1.f, true);
        }

        // Right knob held: show voices and load.
        show_load_ = Down(kKnobs[kVolume].push);

        // Play starts all loops on the press (so a start lands where it was
        // meant), and stops them when let go, unless it listened or muted (or
        // CHOMPI was held). Listening only means something while they run.
        // Loop held + play: the selected channel's figure is cleared (the
        // switch on all: every channel's), and play neither starts nor stops.
        if (Pressed(int(Sw::KEY_27)) && loop_down_) {
            play_down_ = true;
            play_used_ = true;
            Clear(all_);
            loop_used_ = true;
        } else if (Pressed(int(Sw::KEY_27))) {
            play_down_ = true;
            play_used_ = chompi;
            // CHOMPI + play while a page guides: leave the lesson. Its lights go
            // out until the page has stopped guiding (and starts again).
            if (chompi && hint_) {
                hint_ = 0;
                hint_off_ = true;
                Event(kEvLeave, 0, 0);
            }
            if (!running_ && !chompi) {
                running_ = true;
                play_used_ = true;
            }
        }
        if (play_down_ && !play_held) {
            play_down_ = false;
            if (!play_used_) running_ = !running_;
        }
        // A white key pressed with play: let go within kListenBlocks, a tap, it
        // mutes the channel or unmutes it; held longer, it listens to it, for as
        // long as play and the key are held.
        if (!play_held) listen_ = 0;
        for (int k = 0; k < 25; ++k) {
            if (!kKeys[k].white) continue;
            const int c = WhiteIndex(k);
            if (Down(int(kKeys[k].sw))) {
                if (white_down_[c] >= 0 && ++white_down_[c] == kListenBlocks && play_held) listen_ |= 1u << c;
            } else {
                if (white_down_[c] >= 0 && white_down_[c] < kListenBlocks) muted_[c] = !muted_[c];
                white_down_[c] = -1;
                listen_ &= ~(1u << c);
            }
        }

        // Loop: a press turns what was just played on the selected channel into
        // its loop, at once (CHOMPI + loop: adds to the loop). Loop held + play:
        // clears it (see above); holding loop alone never does.
        if (Pressed(int(Sw::KEY_28))) {
            loop_down_ = true;
            loop_used_ = false;
            loop_hold_ = 0;
            loop_ch_ = sel_;
            loop_chompi_ = chompi;
            loop_t_ = now_;
            loop_since_ = hist_[sel_].since;
            loop_ratio_ = ch_[sel_].ratio;
            loop_span_ = Span(sel_, now_);
            loop_kept_ = Capture(sel_, now_, chompi) > 0;
            shift_saved_ = false;
        }
        // While loop is held (a moment, so a tap does not flash them): the lengths
        // on the black keys, for loop + a black key.
        len_show_ = kTestBuild && loop_down_ && !loop_chompi_ && !chompi && !vshow_ && !shift_show_ && loop_hold_ >= kLenShowBlocks;
        if (loop_down_) {
            if (!loop_held) {
                loop_down_ = false;
                vshow_ = false;
                shift_show_ = false;
                // A tap that kept nothing: say so (two dim blinks of its light).
                if (!loop_kept_ && !loop_used_ && loop_hold_ < kTapBlocks) nokeep_ = kNoKeepSeconds;
            } else {
                loop_hold_++;
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
                if (kDrift && align_) Align(); // (aligned: a new length or figure on this beat)
                ApplyPending(!kDrift); // (drift: a player's length at its own loop start, see Advance)
                // Before the first figure: a click on every L0, to play the first one to.
                // Only L0 itself: there is no metre to count beats in.
                if (!AnyFigure()) click_ = 0;
            }
            for (int c = 0; c < kChannels; ++c) Advance(c, block);
        }
        // The keys show where the player is against its place, live (it is
        // pulled or glides there): while loop is held, or it glides.
        // (Looking, CHOMPI + play held: the place, again a moment after a value.)
        if (kDrift && chompi && play_held &&
            (meter_phase_ ? meter_wait_ == 0 || meter_ch_ != sel_ : meter_wait_ < kMeterBlocks * 2 / 3)) ShowPhase();
        if (meter_phase_ && meter_wait_ > 0) {
            meter_v_ = 2.f * Off(meter_ch_);
            if ((chompi && play_held) || glide_[meter_ch_]) meter_wait_ = kMeterBlocks;
        }

        // Reboot: CHOMPI + lowest and highest white key, held 2 s, then let go.
        const bool combo = chompi && Down(int(Sw::KEY_1)) && Down(int(Sw::KEY_15));
        reboot_hold_ = combo ? reboot_hold_ + 1 : 0;
        if (reboot_hold_ > kRebootBlocks) reboot_armed_ = true;
        if (reboot_armed_ && !AnyKeyHeld()) reboot_now_ = true;

        // Parameter changes reach the ringing voices once per block.
        for (int c = 0; c < kChannels; ++c)
            if (dirty_[c]) { dirty_[c] = false; UpdateVoices(c); }

        now_ += block;
    }

    // ---- Sound (audio callback) ----------------------------------------------
    // l, r: the dry sound; sl, sr: what goes into the room (each player's by its distance).
    void Process(float* l, float* r, float* sl, float* sr, size_t n) {
        for (int v = 0; v < kVoices; ++v) {
            Slot& s = slot_[v];
            if (!s.v.Active()) continue;
            const Channel& c = ch_[s.ch];
            // The ensemble's level scales every channel's own.
            const float level = c.level * gain_ * preset_gain_[c.preset] * gate_[s.ch];
            float* bus = bus_[s.ch];
            if (!busy_[s.ch]) { std::fill(bus, bus + n, 0.f); busy_[s.ch] = true; }
            float peak = 0.f;
            for (size_t i = 0; i < n; ++i) {
                const float y = s.v.Process();
                peak = std::max(peak, std::fabs(y));
                bus[i] += y * level;
            }
            // How loud it sounds, for the keys (the instrument's own loudness, not the channel's level).
            s.energy = s.v.Active() ? peak * preset_gain_[c.preset] : 0.f;
        }
        // Each channel at its pan.
        for (int c = 0; c < kChannels; ++c) {
            if (!busy_[c]) continue;
            pan_[c].Process(bus_[c], l, r, sl, sr, n);
            busy_[c] = false;
        }
        // The click: a short high blip, in the middle and dry (not in the room).
        if (click_ >= 0) {
            const int len = int(kClickSeconds * sr_);
            const float w = 2.f * float(M_PI) * kClickHz / sr_, fall = std::exp(-1.f / (kClickDecay * sr_));
            float env = kClickLevel * std::pow(fall, float(click_));
            for (size_t i = 0; i < n && click_ < len; ++i, ++click_, env *= fall) {
                const float y = env * std::sin(w * float(click_));
                l[i] += y;
                r[i] += y;
            }
            if (click_ >= len) click_ = -1;
        }
    }

    // Any player has a figure (the click stops with the first).
    bool AnyFigure() const {
        for (int c = 0; c < kChannels; ++c)
            if (loop_[c].count > 0) return true;
        return false;
    }

    // Load guard: the callback is too slow, drop the quietest voice, as heard
    // (one fading out anyway first): it is missed least.
    void ShedVoice() {
        int quietest = -1;
        float least = 0.f;
        for (int v = 0; v < kVoices; ++v) {
            const Slot& s = slot_[v];
            if (!s.v.Active()) continue;
            const float e = s.v.Fading() ? 0.f : s.energy * ch_[s.ch].level * gate_[s.ch];
            if (quietest < 0 || e < least) { quietest = v; least = e; }
        }
        if (quietest >= 0) { slot_[quietest].v.Stop(); shed_++; }
    }

    uint32_t Misreads() const { return misreads_; }

    // The USB link as the CHOMPI sees it, for the K6 view (Link sets it).
    struct UsbView { bool heard, rx, tx_stuck, waiting; int state; }; // state: 0 off, 1 asleep, 2 set up
    void SetUsbView(const UsbView& v) { usb_view_ = v; }

    int ActiveVoices() const {
        int n = 0;
        for (const auto& s : slot_) n += s.v.Active();
        return n;
    }

    float Room() const { return room_; }
    float Volume() const { return volume_; }
    bool RebootNow() const { return reboot_now_; }
    // Options.h, Align To L0: a new length or a figure from the library on the
    // next L0 beat, the player back on its place (true), or at the player's own
    // loop start (false). Drift builds only; without drift everything is in step.
    void SetAlign(bool on) { align_ = on; }
    void SetLoad(float load) { load_ = load; }

    // ---- The link (main loop): a page on a computer follows over USB-MIDI ------
    // Controls by id: the 25 keys low to high (0-24), CHOMPI 25, play 26, loop
    // 27, the knobs K1-K4 28-31, the wheel 32, K6 33, the switch 34.
    static constexpr int kIdChompi = 25, kIdKnob = 28, kIdSwitch = 34;
    enum EventKind { kEvPress = 1, kEvRelease, kEvTurn, kEvSwitch, kEvBeat, kEvNote, kEvLeave };

    // The next input or beat, three 7-bit bytes: kind, id, value (a turn's
    // clicks + 64; the switch 1 on all; a beat its count).
    bool PopEvent(uint8_t* e) {
        if (ev_tail_ == ev_head_) return false;
        std::copy(ev_[ev_tail_], ev_[ev_tail_] + 3, e);
        __sync_synchronize();
        ev_tail_ = (ev_tail_ + 1) % kEvents;
        return true;
    }

    // Lights the controls in `mask` (a bit per id) for the page; after the
    // lesson was left on the CHOMPI, only once the page has sent none.
    void SetHint(uint64_t mask) {
        if (!mask) hint_off_ = false;
        hint_ = hint_off_ ? 0 : mask;
    }

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
        o[n++] = Q7(size_ * .5f);
        o[n++] = Q7(gain_ * .5f);
        for (int i = 0; i < 6; ++i) o[n++] = 64; // was the ensemble's shifts, now in every channel
        uint32_t muted = 0;
        for (int c = 0; c < kChannels; ++c) muted |= uint32_t(muted_[c]) << c;
        for (uint32_t m : {muted, listen_}) {
            o[n++] = m & 127;
            o[n++] = (m >> 7) & 127;
            o[n++] = (m >> 14) & 127;
        }
        o[n++] = uint8_t(Held(int(Sw::KEY_26)) | Held(int(Sw::KEY_27)) << 1 | Held(int(Sw::KEY_28)) << 2);
        // How busy: the load (0..1), the voices sounding, voices cut by the load guard so far.
        o[n++] = Q7(load_);
        o[n++] = uint8_t(ActiveVoices());
        o[n++] = uint8_t(shed_ & 127);
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
        o[n++] = Q7(.5f + .5f * ch.pan);
        for (float v : {ch.p.strike, ch.p.brightness, ch.p.decay, ch.p.shimmer, ch.p.variation, ch.p.buzz})
            o[n++] = Q7(v);
        const Loop& lp = loop_[c];
        int sounding = 0;
        for (int i = 0; i < lp.count; ++i) sounding += !lp.n[i].silent;
        o[n++] = uint8_t(sounding);
        for (int i = 0; i < lp.count; ++i) {
            if (lp.n[i].silent) continue;
            const int ph = std::min(int(lp.n[i].phase * 16384.f), 16383);
            o[n++] = ph & 127;
            o[n++] = ph >> 7;
            o[n++] = uint8_t(std::min(std::max(lp.n[i].note, 0), 127));
        }
        return n;
    }
    static constexpr int kDescribeMax = 14 + 3 * kLoopNotes;

    // Where everyone is: L0's place and each channel's place in its loop (0..1 as 14 bits).
    int DescribeClock(uint8_t* o) const {
        int n = 0;
        auto put = [&](float x) { const int v = std::min(std::max(int(x * 16384.f), 0), 16383); o[n++] = v & 127; o[n++] = v >> 7; };
        put(l0_pos_);
        for (int c = 0; c < kChannels; ++c) put(loop_[c].pos);
        return n;
    }

    // ---- LEDs (main loop) ------------------------------------------------------
    void DrawLeds(Hardware& hw, float dt) {
        using namespace chompi;
        const bool chompi = Down(int(Sw::KEY_26));
        const float* sel_col = kPresetColor[ch_[sel_].preset];
        // The control the page points at flickers white over what it shows, four
        // times a second: quicker than any beat, so it never reads as one.
        const float hint_glow = std::fmod(led_time_, .25f) < .125f ? .8f : 0.f;
        hint_glow_ = hint_glow;
        // How loud each key of the selected channel sounds now, 0..1 (a square
        // root, so a fading tail stays visible).
        float key_energy[25] = {};
        for (const Slot& sl : slot_) {
            if (sl.ch != sel_ || sl.energy <= 0.f) continue;
            const int note = sl.note - 12 * ch_[sel_].octave;
            for (int k = 0; k < 25; ++k)
                if (kKeys[k].note == note) key_energy[k] = std::max(key_energy[k], std::sqrt(std::min(1.f, sl.energy / kKeyFull)));
        }

        for (int k = 0; k < 25; ++k) {
            float rgb[3] = {0.f, 0.f, 0.f};
            if (reboot_armed_) {
                rgb[0] = rgb[1] = rgb[2] = 1.f;
            } else if (vshow_ && !kKeys[k].white) {
                // Stepping through the versions: one black key each, the oldest
                // lowest, the one playing bright.
                const Ring& r = vshow_all_ ? ver_->ering : ver_->ring[sel_];
                const int b = BlackIndex(k);
                rgb[0] = rgb[1] = rgb[2] = b < r.count ? (b == r.cur ? 1.f : .12f) : 0.f;
            } else if (kLibrary && lib_show_ > 0 && lib_show_fig_ >= 0) {
                // Moving on in the library: the figure's notes, in the chosen
                // player's colour (fading out at the end, as the meter does).
                const Figure& f = LibFig(lib_show_fig_);
                bool on = false;
                for (int i = 0; i < f.count && !on; ++i) on = f.n[i].note == kKeys[k].note;
                const float b = on ? std::min(1.f, lib_show_ / kMeterFadeBlocks) : 0.f;
                for (int j = 0; j < 3; ++j) rgb[j] = sel_col[j] * b;
            } else if (len_show_ && !kKeys[k].white) {
                // Loop held: the lengths. The figure's own white; in the channel's
                // colour every shorter one (what was kept wraps into it) and every
                // longer one that reaches back far enough for what was played;
                // the rest dim.
                const int b = BlackIndex(k);
                if (b == ch_[sel_].ratio) {
                    rgb[0] = rgb[1] = rgb[2] = 1.f;
                } else if (loop_span_ > 0.f && (kRatios[b] <= kRatios[loop_ratio_] || kRatios[b] * Beat() >= loop_span_)) {
                    for (int j = 0; j < 3; ++j) rgb[j] = sel_col[j] * .45f;
                } else {
                    rgb[0] = rgb[1] = rgb[2] = .04f;
                }
            } else if (show_load_) {
                // A bar of active voices from the lowest key; green, yellow, red by
                // load. (Before the meter: turning K6 while holding it shows the meter
                // otherwise.)
                if (k < ActiveVoices()) {
                    const float* col = load_ < .6f ? kGreen : load_ < .85f ? kYellow : kRed;
                    std::copy(col, col + 3, rgb);
                }
                // The five highest keys: the USB link, for finding out why a page
                // hears nothing. 25 a page said hello (3 s), 24 data comes in, 23
                // sending is stuck, 22 messages wait to be read, 21 the computer
                // has set the CHOMPI up (yellow: asleep).
                const UsbView& u = usb_view_;
                const float dim[3] = {.15f, 0.f, 0.f}, white[3] = {1.f, 1.f, 1.f}, off[3] = {0.f, 0.f, 0.f};
                const float* show = nullptr;
                if (k == 24) show = u.heard ? kGreen : dim;
                else if (k == 23) show = u.rx ? white : off;
                else if (k == 22) show = u.tx_stuck ? kRed : kGreen;
                else if (k == 21) show = u.waiting ? kYellow : off;
                else if (k == 20) show = u.state == 2 ? kGreen : u.state == 1 ? kYellow : kRed;
                if (show) std::copy(show, show + 3, rgb);
            } else if (meter_wait_ > 0) {
                // The meter, with the selected channel's notes over it in its colour.
                static constexpr float kWhite[3] = {1.f, 1.f, 1.f};
                MeterKey(k, meter_white_ ? kWhite : sel_col, rgb);
                for (int j = 0; j < 3; ++j) rgb[j] = std::max(rgb[j], sel_col[j] * key_flash_[k]);
            } else if (sound_view_ && kKeys[k].white) {
                // The set's instruments on the white keys, in their colours; the channel's bright.
                const int s = kSetPresets[view_set_][WhiteIndex(k)];
                if (s >= 0) {
                    const float b = s == ch_[sel_].preset ? 1.f : .1f;
                    for (int j = 0; j < 3; ++j) rgb[j] = kPresetColor[s][j] * b;
                }
            } else if (sound_view_) {
                // The sets on the black keys: the one shown bright.
                const int b = BlackIndex(k);
                if (kSets > 1 && b < kSets) rgb[0] = rgb[1] = rgb[2] = b == view_set_ ? 1.f : .1f;
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
                // all: the ensemble's) bright; one waiting for the next beat swells,
                // slower than the page's flicker, so the two never look alike.
                const int b = BlackIndex(k);
                float v = b == (all_ ? ens_ : ch_[sel_].ratio) ? 1.f : .08f;
                if (b == (all_ ? pending_ens_ : pending_[sel_])) v = .1f + .9f * (.5f + .5f * std::cos(led_time_ * 2.f * float(M_PI) * 1.5f));
                rgb[0] = rgb[1] = rgb[2] = v;
            } else if (oct_show_ > 0 && OctaveKey(k) >= -2) {
                // The octave on the five lowest black keys, -2 .. +2: the current one bright.
                const float b = OctaveKey(k) == ch_[sel_].octave ? 1.f : .1f;
                rgb[0] = rgb[1] = rgb[2] = b;
            } else {
                // The selected channel's notes glow as long as they sound, as bright as they are loud.
                const float e = std::max(key_flash_[k], key_energy[k]);
                for (int j = 0; j < 3; ++j) rgb[j] = sel_col[j] * e;
                if (sel_glow_ > 0.f && kKeys[k].white && WhiteIndex(k) == sel_) {
                    const float g = .7f * std::min(1.f, sel_glow_ / .15f);
                    for (int j = 0; j < 3; ++j) rgb[j] = std::max(rgb[j], sel_col[j] * g);
                }
            }
            if ((hint_ >> k) & 1)
                for (float& x : rgb) x = std::max(x, hint_glow);
            SetSmtLedFloat(kKeys[k].led, rgb[0], rgb[1], rgb[2]);
            key_flash_[k] *= std::exp(-dt / .25f);
        }

        // Knobs: the selected channel's colour; with the switch on all, four
        // different colours, steady (no one channel looks like that: everyone).
        led_time_ += dt;
        static constexpr float kAllCol[4][3] = {{1.f, .35f, 0.f}, {1.f, .85f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
        for (int n = 0; n < 4; ++n) {
            const float* kc = all_ ? kAllCol[n] : sel_col;
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

        // Play: lit while the loops run. Loop: the selected channel has a loop,
        // and a beat each time it comes round; without one, nearly dark.
        Pth(kPlayLed, 0.f, running_ ? .5f : 0.f, 0.f);
        const float lb = loop_[sel_].count ? .25f + (running_ ? .6f * ch_flash_[sel_] : 0.f) : .03f;
        if (cleared_ > 0.f) {
            const float r = cleared_ / kClearedSeconds; // cleared: red, fading out
            Pth(kLoopLed, r, 0.f, 0.f);
        } else if (nokeep_ > 0.f) {
            // Nothing to keep: two dim white blinks.
            const float w = std::fmod(kNoKeepSeconds - nokeep_, .3f) < .15f ? .35f : 0.f;
            Pth(kLoopLed, w, w, w);
        } else {
            float b = std::max(lb, kept_flash_);
            // A figure from the library waiting for the player's loop start: the
            // loop light swells, as a length waiting on its black key.
            bool waits = false;
            for (int c = 0; c < kChannels; ++c) waits |= lib_next_[c] >= 0 && (all_ || c == sel_);
            if (kLibrary && waits) b = .1f + .9f * (.5f + .5f * std::cos(led_time_ * 2.f * float(M_PI) * 1.5f));
            Pth(kLoopLed, sel_col[0] * b, sel_col[1] * b, sel_col[2] * b);
        }
        kept_flash_ *= std::exp(-dt / .2f);
        nokeep_ = std::max(nokeep_ - dt, 0.f);
        cleared_ = std::max(cleared_ - dt, 0.f);
        sel_glow_ = std::max(sel_glow_ - dt, 0.f);
        fill_led_data();

        const float fade = std::exp(-dt / .15f);
        l0_flash_ *= fade;
        for (float& f : ch_flash_) f *= fade;
        const float note_fade = std::exp(-dt / .1f);
        for (float& f : note_flash_) f *= note_fade;
    }

private:
    struct Slot { AnyVoice v; int ch, note; uint32_t age = 0; float energy = 0.f; };
    // The set a preset is in: the one shown, if it is there, else the first.
    static int SetOf(int preset, int shown) {
        for (int i = 0; i < 15; ++i) if (kSetPresets[shown][i] == preset) return shown;
        for (int s = 0; s < kSets; ++s)
            for (int i = 0; i < 15; ++i) if (kSetPresets[s][i] == preset) return s;
        return 0;
    }

    // A light at the top, with the page's hint over it: CHOMPI 0, K1-K4 1-4,
    // the wheel 5 and 6, play 7, loop 8, K6 9.
    uint64_t hint_ = 0;
    bool hint_off_ = false; // the lesson was left (CHOMPI + play)
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
    //     from its start, the ensemble's level and the circle's size back to neutral;
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
            Save(c);
            Channel& ch = ch_[c];
            ch.preset = std::min(int(d[2]), kNumMalletPresets - 1);
            ch.p = kMalletPresets[ch.preset].p;
            ch.octave = std::min(std::max(int(d[3]) - 2, -2), 2);
            ch.ratio = std::min(int(d[4]), kNumRatios - 1);
            ch.level = d[5] / 127.f;
            ch.pan = std::min(std::max((d[6] - 64) / 63.f, -1.f), 1.f);
            ch.slide = 0.f;
            lib_at_[c] = lib_next_[c] = -1; // a figure from the page, not from the library
            Place(c, true);
            muted_[c] = d[7];
            pending_[c] = -1;
            Loop& lp = loop_[c];
            lp.count = 0;
            for (int i = 0; i < d[8] && 9 + 3 * i + 2 < n && lp.count < kLoopNotes; ++i) {
                const uint8_t* e = d + 9 + 3 * i;
                const float ph = (e[0] | e[1] << 7) / 16384.f;
                lp.n[lp.count++] = {ph * Len(c), e[2], ph};
            }
            lp.pos = 0.f;
            lp.seam = 0.f;
            Kept(c);
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
                size_ = 1.f;
            gain_ = 1.f;
            PlaceAll(true);
            UpdateAll();
            Restart();
        } else if (d[0] == 5) {
            SaveAll();
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
    static constexpr float kHandOverSeconds = .05f; // a sustained note struck again: the old one fades this fast
    static constexpr float kKeyFull = .3f;          // a voice this loud lights its key fully
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

    // Loop held + play: channel loop_ch_'s figure cleared (everyone: every
    // channel's); a cleared channel starts over at 1, in step.
    void Clear(bool everyone) {
        if (everyone) SaveAll();
        for (int c = 0; c < kChannels; ++c) {
            if (!everyone && c != loop_ch_) continue;
            Save(c);
            loop_[c].count = 0;
            ch_[c].slide = 0.f;
            hist_[c].since = now_;
            ch_[c].ratio = kRatioOne;
            pending_[c] = -1;
            detune_[c] = 0.f;
            glide_[c] = false;
            lib_next_[c] = -1; // (it stays where it was in the library)
        }
        cleared_ = kClearedSeconds;
    }
    static constexpr float kClearedSeconds = .5f; // the loop light red, fading, after a clear
    float cleared_ = 0.f;


    static constexpr float kMinL0 = .5f, kMaxL0 = 8.f; // seconds
    static constexpr float kTempoStep = 1.01f;         // the wheel: 1% per click
    static constexpr float kStartL0 = 2.f;             // L0 at power-on, s
    static constexpr float kFeelSeconds = .03f;        // feel 1: repeats up to 30 ms off
    // Drift: detune 0.1% a click up to 5%; the pull 0.5% a click up to 20%, 4% at
    // power-on; gliding home pulls with at least 30% and no detune, until it
    // is within a 256th of its loop; the speed stays within half and 1.5 times.
    static constexpr float kDetuneStep = .001f, kMaxDetune = .05f;
    static constexpr float kPullStep = .005f, kMaxPull = .2f, kStartPull = .04f;
    static constexpr float kGlidePull = .3f, kHome = 1.f / 256;
    static constexpr float kSlowest = .5f, kFastest = 1.5f;

    static int BlackIndex(int k) {
        int b = 0;
        for (int i = 0; i < k; ++i) b += !kKeys[i].white;
        return b;
    }

    // On the beat: the ensemble's new length (a tempo: every loop keeps its
    // place and its notes, all play faster or slower together) and each
    // channel's new ratio, a new window on the same notes: notes past the new
    // end wrap round, and unfold again when the loop grows. The loop goes on
    // from where it is in time; past the new end, wrapped round, as its notes.
    // players: the channels' new lengths too (drift: only when stopped).
    void ApplyPending(bool players = true) {
        float was[kChannels];
        for (int c = 0; c < kChannels; ++c) was[c] = Len(c);
        if (pending_ens_ >= 0) ens_ = pending_ens_;
        pending_ens_ = -1;
        for (int c = 0; c < kChannels; ++c) {
            if (pending_[c] >= 0 && players) {
                if (pending_[c] != ch_[c].ratio) Save(c);
                ch_[c].ratio = pending_[c];
                pending_[c] = -1;
            }
            if (Len(c) == was[c]) continue;
            Loop& lp = loop_[c];
            const float p = lp.pos * was[c] / Len(c);
            lp.pos = p - std::floor(p);
            Place(c);
        }
    }

    // Each note's place in channel c's loop from its time; past the end: wrapped
    // round to the beginning.
    void Place(int c) {
        Loop& lp = loop_[c];
        const float len = Len(c);
        for (int i = 0; i < lp.count; ++i) {
            LoopNote& n = lp.n[i];
            const float p = n.at / len;
            n.phase = p - std::floor(p);
            n.silent = false;
            n.armed = true;
            n.jit = 0.f;
        }
    }

    // The beat: L0 times the ensemble's length, a tempo: every figure stays whole
    // and plays faster or slower. A channel's loop: the beat times its ratio
    // (Len, in beats).
    float Beat() const { return l0_ * kRatios[ens_]; }
    float Len(int c) const { return kRatios[ch_[c].ratio]; }
    float LoopSamples(int c) const { return Beat() * Len(c); }

    void Remember(int c, int note) {
        History& h = hist_[c];
        h.e[h.head] = {now_, note};
        h.head = (h.head + 1) % kHistory;
        if (h.count < kHistory) h.count++;
    }

    // Loop pressed at time t: the notes played on channel c in the loop length
    // before t, and since the last loop press, become its loop (or join it).
    // window: how far back (0: the loop's length); longer than the loop, the
    // older notes wrap round into it.
    int Capture(int c, uint32_t t, bool add, bool save = true, float window = 0.f) {
        History& h = hist_[c];
        Loop& lp = loop_[c];
        const float len = LoopSamples(c);
        // A new loop begins where its window began, window before t. Added to,
        // it goes on from where it is.
        const bool fresh = !add || lp.count == 0;
        if (!fresh || window <= 0.f) window = len;
        float pos = lp.pos;
        if (fresh) {
            pos = (float(now_ - t) + window) / len;
            pos -= std::floor(pos);
        }
        LoopNote got[kLoopNotes];
        int n = 0;
        for (int i = 0; i < h.count; ++i) {
            const Played& e = h.e[(h.head - h.count + i + kHistory) % kHistory];
            if (int32_t(e.t - h.since) <= 0 || int32_t(t - e.t) < 0 || float(t - e.t) >= window) continue;
            // Its place: as far behind the loop's position now as it was played
            // before now. A new loop: its time from the window's beginning, in
            // beats, wrapped round where the window is longer than the loop.
            float ph = pos - float(now_ - e.t) / len;
            ph -= std::floor(ph);
            const float at = fresh ? (window - float(t - e.t)) / Beat() : ph * Len(c);
            // Kept without the channel's octave: the loop follows the octave, as it
            // follows the other settings.
            const int note = e.note - 12 * ch_[c].octave;
            const LoopNote ln = {at, note, ph};
            if (n < kLoopNotes) got[n++] = ln;
            else { std::copy(got + 1, got + kLoopNotes, got); got[kLoopNotes - 1] = ln; }
        }
        if (n == 0) return 0; // nothing played: the loop stays as it is
        if (save) Save(c);
        h.since = t;
        // Kept: the loop light flashes in the channel's colour, the notes once across the keys.
        kept_flash_ = 1.f;
        for (int i = 0; i < n; ++i) FlashNote(got[i].note);
        if (fresh) {
            ch_[c].slide = 0.f;
            lp.count = 0;
            lp.pos = pos;
            Kept(c);
            // Where the window began after an L0 beat.
            float b = l0_pos_ - (float(now_ - t) + window) / Beat();
            lp.seam = b - std::floor(b);
        }
        for (int i = 0; i < n; ++i) {
            bool twin = false; // the same note at the same place is already there
            for (int j = 0; j < lp.count; ++j)
                if (lp.n[j].note == got[i].note && !lp.n[j].silent && std::fabs(lp.n[j].phase - got[i].phase) < .01f) twin = true;
            if (!twin && lp.count < kLoopNotes) lp.n[lp.count++] = got[i];
        }
        if (kLibrary) LibAdd(c);
        return n;
    }

    // Loop held + a black key: the playing before loop went down, kept again
    // with that length (it replaces what this hold kept; the version saved by
    // the hold's first keep stays the one before). Shorter than the figure's
    // own length: what loop kept (its own length back from the press) wraps
    // into it, as a new length would; longer: it reaches back that far.
    void Rekeep(int c, int ratio) {
        loop_used_ = true; // (not a tap that kept nothing)
        const int was = ch_[c].ratio;
        hist_[c].since = loop_since_;
        ch_[c].ratio = ratio;
        pending_[c] = -1;
        const float window = Beat() * std::max(kRatios[ratio], kRatios[loop_ratio_]);
        if (Capture(c, loop_t_, false, !loop_kept_, window) > 0) {
            loop_kept_ = true;
            Place(c);
        } else {
            ch_[c].ratio = was;
            nokeep_ = kNoKeepSeconds;
        }
    }

    // How far back the playing on channel c reaches at time t (since its last
    // keep, at most the longest length): a length fits when it is this long.
    float Span(int c, uint32_t t) const {
        const History& h = hist_[c];
        const float longest = Beat() * kRatios[kNumRatios - 1];
        float span = 0.f;
        for (int i = 0; i < h.count; ++i) {
            const Played& e = h.e[(h.head - h.count + i + kHistory) % kHistory];
            if (int32_t(e.t - h.since) <= 0 || int32_t(t - e.t) < 0) continue;
            const float ago = float(t - e.t);
            if (ago < longest) span = std::max(span, ago);
        }
        return span;
    }

    void Advance(int c, size_t block) {
        Loop& lp = loop_[c];
        float step = block / LoopSamples(c);
        if (kDrift) step *= Speed(c);
        // The library: the next figure comes in at the player's own loop start,
        // the old one played to its end.
        if (kLibrary && !align_ && lib_next_[c] >= 0 && lp.pos + step >= 1.f) {
            const float rest = lp.pos + step - 1.f, was = Len(c);
            Sweep(c, 1.f);
            Take(c, lib_next_[c], 0.f);
            Sweep(c, rest * was / Len(c));
            return;
        }
        // Drift: a new length waits for the player's own loop start, where
        // the time in its loop and its place agree, and goes on from there.
        if (kDrift && !align_ && pending_[c] >= 0 && lp.pos + step >= 1.f) {
            const float rest = lp.pos + step - 1.f, was = Len(c);
            Sweep(c, 1.f);
            if (pending_[c] != ch_[c].ratio) Save(c);
            ch_[c].ratio = pending_[c];
            pending_[c] = -1;
            Place(c);
            Sweep(c, rest * was / Len(c));
            return;
        }
        Sweep(c, lp.pos + step);
    }

    // Channel c's loop on from where it is to `to` (1 and over: round past its
    // start), its notes struck on the way.
    void Sweep(int c, float to) {
        Loop& lp = loop_[c];
        const float from = lp.pos;
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
            if (n.silent) continue;
            if (!n.armed) {
                if (crossed(n.phase + .5f)) n.armed = true; // half a loop later: ready again
                continue;
            }
            if (!crossed(n.phase + n.jit)) continue;
            n.armed = false;
            n.jit = jit_max * (Rand() + Rand() - 1.f);
            if (Rand() < skip) continue;
            NoteOn(c, n.note + 12 * ch_[c].octave, swell_[c] * (1.f - feel * .35f * Rand()));
            // (Its key glows with the sound: see DrawLeds.)
        }
    }

    // ---- Versions ----------------------------------------------------------------
    // The figure of channel c as it is now, into f.
    void Keep(int c, Figure& f) const {
        const Loop& lp = loop_[c];
        std::copy(lp.n, lp.n + lp.count, f.n);
        f.count = lp.count;
        f.seam = lp.seam;
        f.ratio = ch_[c].ratio;
        f.pos = lp.pos;
        f.t = now_;
    }
    // The same notes, length and seam (where the loop is does not matter).
    static bool Same(const Figure& a, const Figure& b) {
        if (a.count != b.count || a.ratio != b.ratio || a.seam != b.seam) return false;
        for (int i = 0; i < a.count; ++i)
            if (a.n[i].note != b.n[i].note || a.n[i].at != b.n[i].at || a.n[i].silent != b.n[i].silent) return false;
        return true;
    }
    // Which of channel c's versions the figure is now, or -1.
    int Find(int c) const {
        const Ring& r = ver_->ring[c];
        Figure now;
        Keep(c, now);
        for (int k = r.count - 1; k >= 0; --k)
            if (Same(now, ver_->f[c][Nth(r, kVersions, k)])) return k;
        return -1;
    }
    // A saved figure back on channel c, where its loop would be had it gone on
    // playing since it was saved (at the length it had).
    void Bring(int c, const Figure& f) {
        Loop& lp = loop_[c];
        std::copy(f.n, f.n + f.count, lp.n);
        lp.count = f.count;
        lp.seam = f.seam;
        ch_[c].ratio = f.ratio;
        pending_[c] = -1;
        const float p = f.pos + float(now_ - f.t) / LoopSamples(c);
        lp.pos = p - std::floor(p);
        Anchor(c);
        Place(c);
    }
    // A new slot at the newest end of a ring (the oldest goes when it is full).
    static int Push(Ring& r, int cap) {
        if (r.count < cap) return (r.head + r.count++) % cap;
        const int i = r.head;
        r.head = (r.head + 1) % cap;
        return i;
    }
    static int Nth(const Ring& r, int cap, int k) { return (r.head + k) % cap; } // k: from the oldest
    // Before channel c's figure changes: save it, unless it is a version already
    // (stepped to) or empty. Either way it is the figure as it is from now on.
    void Save(int c) {
        Ring& r = ver_->ring[c];
        if (r.cur < 0 && loop_[c].count > 0 && Find(c) < 0) Keep(c, ver_->f[c][Push(r, kVersions)]);
        r.cur = -1;
        ver_->ering.cur = -1;
    }
    // Which of the ensemble's versions everything is now, or -1.
    int FindAll() const {
        const Ring& r = ver_->ering;
        Figure now;
        for (int k = r.count - 1; k >= 0; --k) {
            bool same = true;
            for (int c = 0; c < kChannels && same; ++c) {
                Keep(c, now);
                same = Same(now, ver_->e[Nth(r, kEnsembleVersions, k)][c]);
            }
            if (same) return k;
        }
        return -1;
    }
    void SaveAll() {
        Ring& r = ver_->ering;
        bool any = false;
        for (int c = 0; c < kChannels; ++c) any |= loop_[c].count > 0;
        if (r.cur < 0 && any && FindAll() < 0) {
            const int i = Push(r, kEnsembleVersions);
            for (int c = 0; c < kChannels; ++c) Keep(c, ver_->e[i][c]);
        }
        r.cur = -1;
    }
    // One version older (dir -1) or newer (+1). The first step saves the figure
    // as it is, so stepping back to the newest finds it again.
    void Browse(int c, int dir) {
        Ring& r = ver_->ring[c];
        if (r.cur < 0) {
            r.cur = Find(c); // already a version: no new one
            if (r.cur < 0) {
                Keep(c, ver_->f[c][Push(r, kVersions)]);
                r.cur = r.count - 1;
            }
        }
        const int to = std::min(std::max(r.cur + dir, 0), r.count - 1);
        if (to == r.cur) return;
        r.cur = to;
        Bring(c, ver_->f[c][Nth(r, kVersions, to)]);
        ver_->ering.cur = -1;
    }
    void BrowseAll(int dir) {
        Ring& r = ver_->ering;
        if (r.cur < 0) {
            r.cur = FindAll();
            if (r.cur < 0) {
                const int i = Push(r, kEnsembleVersions);
                for (int c = 0; c < kChannels; ++c) Keep(c, ver_->e[i][c]);
                r.cur = r.count - 1;
            }
        }
        const int to = std::min(std::max(r.cur + dir, 0), r.count - 1);
        if (to == r.cur) return;
        r.cur = to;
        const int i = Nth(r, kEnsembleVersions, to);
        for (int c = 0; c < kChannels; ++c) {
            Bring(c, ver_->e[i][c]);
            ver_->ring[c].cur = -1;
        }
    }

    // ---- The library ---------------------------------------------------------------
    // The oldest figure still in it, and one past the newest.
    int LibFirst() const { return std::max(ver_->lib_next - kLibrarySize, 0); }
    const Figure& LibFig(int s) const { return ver_->lib[s % kLibrarySize]; }
    // Channel c's figure as it is now (just kept), into the library; the same as
    // the newest: not again. Either way, c plays it now.
    void LibAdd(int c) {
        Figure f;
        Keep(c, f);
        const int last = ver_->lib_next - 1;
        if (last >= 0 && Same(f, LibFig(last))) {
            lib_at_[c] = last;
        } else {
            ver_->lib[ver_->lib_next % kLibrarySize] = f;
            lib_at_[c] = ver_->lib_next++;
        }
        lib_next_[c] = -1;
    }
    // Channel c one figure on (dir +1) or back (-1), from the one it waits for or
    // else the one it plays; a player with none from the library starts at the
    // oldest. It lands at the player's loop start (stopped: at once). Its notes
    // show on the keys. False: there is no figure that way.
    bool MoveOn(int c, int dir) {
        const int from = lib_next_[c] >= 0 ? lib_next_[c] : lib_at_[c];
        int to = from < 0 ? (dir > 0 ? LibFirst() : -1) : std::max(from, LibFirst() - 1) + dir;
        if (to < LibFirst() || to >= ver_->lib_next) return false;
        lib_next_[c] = to == lib_at_[c] ? -1 : to; // back to the one it plays: nothing waits
        if (c == sel_ || lib_show_fig_ < 0) { lib_show_fig_ = to; lib_show_ = kMeterBlocks; }
        if (!running_ && lib_next_[c] >= 0) { Take(c, to, align_ ? LandPos(to) : 0.f); }
        return true;
    }
    // Aligned (Align To L0), on an L0 beat: every player with a new length or a
    // figure from the library waiting takes it now, back on its place, in step
    // with L0 (what drift had moved goes; its detune stays its own).
    void Align() {
        for (int c = 0; c < kChannels; ++c) {
            if (kLibrary && lib_next_[c] >= 0) {
                Take(c, lib_next_[c], LandPos(lib_next_[c]));
                glide_[c] = false;
            } else if (pending_[c] >= 0) {
                Loop& lp = loop_[c];
                float p = lp.pos + Off(c); // its place, at the old length
                const float was = Len(c);
                if (pending_[c] != ch_[c].ratio) Save(c);
                ch_[c].ratio = pending_[c];
                pending_[c] = -1;
                p = (p - std::floor(p)) * was / Len(c); // on from there in time, as without drift
                lp.pos = p - std::floor(p);
                Place(c);
                Anchor(c);
                glide_[c] = false;
            }
        }
    }
    // Where figure s's loop is now, had it gone on in step with L0 since it was
    // kept (its seam after an L0 beat), as a restart puts it.
    float LandPos(int s) const {
        const Figure& f = LibFig(s);
        const float p = (l0_pos_ - f.seam) / kRatios[f.ratio];
        return p - std::floor(p);
    }
    // Channel c takes figure s, its loop at pos (0: its start): the notes and the
    // length; its sound, octave, level and place stay. It plays it from where it
    // is against L0, its ghost too (nothing pulls it elsewhere).
    void Take(int c, int s, float pos) {
        const Figure& f = LibFig(s);
        Save(c);
        Loop& lp = loop_[c];
        std::copy(f.n, f.n + f.count, lp.n);
        lp.count = f.count;
        ch_[c].ratio = f.ratio;
        ch_[c].slide = 0.f;
        pending_[c] = -1;
        lp.pos = pos;
        // Where this loop starts after an L0 beat, so a restart keeps it there.
        const float b = l0_pos_ - pos * Len(c);
        lp.seam = b - std::floor(b);
        Place(c);
        Kept(c);
        hist_[c].since = now_; // what was played before is not kept with it
        lib_at_[c] = s;
        lib_next_[c] = -1;
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

    // Loop held + wheel: channel c's figure slides in its loop, later (right) or
    // earlier (left), a 64th of its length per click, heard at once; what
    // slides past the end comes in at the beginning. The loop itself stays
    // where it is, in step with L0 (its light still flashes on its start).
    // Each note keeps its slide in beats and its time stays whole (where it
    // falls in the loop is worked out for the length it has now), so another
    // length and back finds everything where it was, and Unslide puts each
    // note back exactly. A note keeps whether it has played this round, so
    // one just heard and pushed a little later does not play again.
    void Slide(int c, int steps) {
        Loop& lp = loop_[c];
        if (!lp.count || !steps) return;
        const float len = Len(c), d = steps * len / kShiftSteps;
        for (int i = 0; i < lp.count; ++i) Move(c, lp.n[i], d);
        ch_[c].slide += d;
    }
    void Unslide(int c) {
        Loop& lp = loop_[c];
        for (int i = 0; i < lp.count; ++i) Move(c, lp.n[i], -lp.n[i].slid);
        ch_[c].slide = 0.f;
    }
    // Loop held + push: the figure back exactly where it sat against L0 when it
    // was kept, its slides undone and any drift too (another length for a
    // while): its loop goes on as if nothing had moved it since.
    double Beats() const { return double(l0_beats_) + double(l0_pos_); }
    void Kept(int c) { kept_beat_[c] = Beats(); kept_pos_[c] = loop_[c].pos; Anchor(c); }

    // Drift. A player's place (its ghost): where its loop would be had it gone
    // on in step with L0, at the length it has now, since it was kept where it
    // is (or set going by Restart). home_: the beat its loop started on then.
    void Anchor(int c) { home_[c] = Beats() - double(loop_[c].pos) * Len(c); }
    // How far its place is ahead of where it is, in loops, -0.5 .. 0.5
    // (above 0: it is behind, its notes come later).
    float Off(int c) const {
        double e = (Beats() - home_[c]) / Len(c) - loop_[c].pos;
        e -= std::floor(e + .5);
        return float(e);
    }
    // How fast it goes now, 1 being in step: its detune and the pull towards
    // its place (gliding home: a strong pull and no detune, until it is there).
    float Speed(int c) {
        const float off = Off(c);
        if (glide_[c] && std::fabs(off) < kHome) glide_[c] = false;
        const float pull = glide_[c] ? std::max(pull_, kGlidePull) : pull_;
        const float m = 1.f + (glide_[c] ? 0.f : detune_[c]) + pull * std::sin(2.f * float(M_PI) * off);
        return std::min(std::max(m, kSlowest), kFastest);
    }
    // Loop held + wheel: a kick, a 64th of its length per click; right: later
    // (the loop back a little, its notes just heard not again), left: earlier
    // (the notes skipped come next time round).
    void Kick(int c, int steps) {
        const float p = loop_[c].pos - float(steps) / kShiftSteps;
        loop_[c].pos = p - std::floor(p);
    }
    // The keys show channel c's place against its ghost (live, see Controls).
    void ShowPhase() {
        ShowMeter(kMeterCentre, 2.f * Off(sel_));
        meter_phase_ = true;
        meter_ch_ = sel_;
    }
    void Back(int c) {
        Unslide(c);
        const double p = kept_pos_[c] + (Beats() - kept_beat_[c]) / Len(c);
        loop_[c].pos = float(p - std::floor(p));
        Place(c);
    }
    void Move(int c, LoopNote& n, float d) {
        n.at += d;
        n.slid += d;
        const float p = n.at / Len(c);
        n.phase = p - std::floor(p);
    }

    // Channel `to` takes over the pattern of `from`: its loop and ratio, in step
    // with it. Everything else stays: `to` plays the figure on its own
    // instrument, in its own octave, with its own level, place and swell.
    void Copy(int from, int to) {
        if (from == to) return;
        Save(to);
        loop_[to] = loop_[from];
        ch_[to].ratio = ch_[from].ratio;
        ch_[to].slide = ch_[from].slide;
        kept_beat_[to] = kept_beat_[from];
        kept_pos_[to] = kept_pos_[from];
        home_[to] = home_[from]; // the same place (its detune stays its own)
        glide_[to] = false;
        pending_[to] = pending_[from];
        lib_at_[to] = lib_at_[from];
        lib_next_[to] = lib_next_[from];
        hist_[to].since = now_; // older notes played on `to` are not looped any more
    }

    bool AnyKeyHeld() const {
        for (const Key& k : kKeys)
            if (Down(int(k.sw))) return true;
        return Down(int(Sw::KEY_26)) || Down(int(Sw::KEY_27)) || Down(int(Sw::KEY_28));
    }

    MalletParams Effective(int c) const { return ch_[c].p; }

    // A knob's layer of the sound: K1 strike | shimmer, K2 brightness | feel,
    // K3 decay | buzz (the second with CHOMPI).
    static float& Param(MalletParams& p, int knob, bool chompi) {
        return knob == 0 ? (chompi ? p.shimmer : p.strike)
             : knob == 1 ? (chompi ? p.variation : p.brightness)
                         : (chompi ? p.buzz : p.decay);
    }
    static float Param(const MalletParams& p, int knob, bool chompi) {
        return Param(const_cast<MalletParams&>(p), knob, chompi);
    }

    // On all: everyone's value of a knob's layer, on average, the mark where
    // their instruments have it.
    void ShowAll(int knob, bool chompi) {
        float v = 0.f, o = 0.f;
        for (int c = 0; c < kChannels; ++c) {
            v += Param(ch_[c].p, knob, chompi);
            o += Param(kMalletPresets[ch_[c].preset].p, knob, chompi);
        }
        ShowMeter(kMeterBar, v / kChannels, o / kChannels, true);
    }

    void NoteOn(int c, int note, float velocity = 1.f) {
        // The same key on the same channel strikes its ringing voice again; an
        // instrument that sounds on hands over to a new voice instead, the old
        // one fading out under it.
        int pick = -1, fading = -1;
        const bool sustained = AnyVoice::Sustained(kMalletPresets[ch_[c].preset].engine);
        for (int v = 0; v < kVoices && pick < 0 && fading < 0; ++v)
            if (slot_[v].v.Active() && slot_[v].ch == c && slot_[v].note == note) {
                if (sustained && AnyVoice::Sustained(slot_[v].v.Engine())) { slot_[v].v.Fade(kHandOverSeconds); fading = v; }
                else pick = v;
            }
        for (int v = 0; v < kVoices && pick < 0; ++v)
            if (!slot_[v].v.Active()) pick = v;
        if (pick < 0) { // steal the oldest (not the one fading out)
            pick = fading == 0 ? 1 : 0;
            for (int v = 0; v < kVoices; ++v)
                if (v != fading && slot_[v].age < slot_[pick].age) pick = v;
        }
        Slot& s = slot_[pick];
        note_flash_[c] = 1.f;
        Event(kEvNote, c, note);
        s.ch = c;
        s.note = note;
        s.age = ++clock_;
        s.v.Strike(MidiToHz(float(note)), kMalletPresets[ch_[c].preset], Effective(c), velocity);
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
    //   K3 decay      | prep (the instrument's preparation; on the mallets the buzz)
    //   K4 level      | pan
    // With the switch on all: K1-K3 turn every channel's own values, each from
    // where it is; K4 the ensemble's level | the circle's size.
    // Every turn shows its value on the keys (the meter).
    void Turn(int knob, int inc, bool chompi) {
        const float d = inc * .02f;
        if (all_) {
            // K4 as on one channel: the level, and with CHOMPI the pan (for
            // all: the width). The mark: as set.
            if (knob == 3) {
                float& v = chompi ? size_ : gain_;
                v = std::min(std::max(v + 2.f * d, 0.f), 2.f);
                if (chompi) PlaceAll(false);
                ShowMeter(kMeterBar, v * .5f, .5f, true);
                return;
            }
            // K1-K3 turn every channel's own value, each from where it is: what a
            // channel shows is what it plays, and CHOMPI + push takes it back.
            for (int c = 0; c < kChannels; ++c) {
                float& v = Param(ch_[c].p, knob, chompi);
                v = Clamp01(v + d);
            }
            ShowAll(knob, chompi);
            UpdateAll();
            return;
        }
        Channel& c = ch_[sel_];
        MalletParams& p = c.p;
        const MalletParams& orig = kMalletPresets[c.preset].p;
        if (knob == 3) {
            if (chompi) {
                // Round and round: 50 clicks a turn. The keys: in front at the
                // middle C, the sides at the quarters, behind at both ends.
                // Left to right in 50 clicks; the keys: the middle C in the middle.
                c.pan = std::min(std::max(c.pan + 2.f * d, -1.f), 1.f);
                Place(sel_, false);
                ShowMeter(kMeterCentre, c.pan);
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

    // CHOMPI + push: the knob's two layers as the instrument has them (K4: the
    // level and the pan at power-on). On all: everyone's back to their instrument, K4 the
    // ensemble's level and the width to the middle.
    void Reset(int knob) {
        if (all_) {
            if (knob == 3) {
                gain_ = 1.f;
                size_ = 1.f;
                PlaceAll(false);
                ShowMeter(kMeterBar, .5f, .5f, true);
                return;
            }
            for (int c = 0; c < kChannels; ++c)
                for (bool second : {false, true})
                    Param(ch_[c].p, knob, second) = Param(kMalletPresets[ch_[c].preset].p, knob, second);
            ShowAll(knob, false);
            UpdateAll();
            return;
        }
        Channel& c = ch_[sel_];
        const MalletParams& o = kMalletPresets[c.preset].p;
        if (knob == 3) {
            c.level = Channel().level;
            c.pan = StartPan(sel_);
            Place(sel_, false);
            ShowMeter(kMeterBar, c.level, c.level);
            return;
        }
        MalletParams& p = c.p;
        if (knob == 0) { p.strike = o.strike; p.shimmer = o.shimmer; }
        if (knob == 1) { p.brightness = o.brightness; p.variation = o.variation; }
        if (knob == 2) { p.decay = o.decay; p.buzz = o.buzz; }
        const float v = knob == 0 ? p.strike : knob == 1 ? p.brightness : p.decay;
        ShowMeter(kMeterBar, v, v);
        Update(sel_);
    }
    // Channel c's pan at power-on: 1 in the middle, then outwards, neighbours
    // on opposite sides (2 left, 3 right, 4 further left ...), the outermost
    // most of the way to either side.
    static float StartPan(int c) {
        const int out = (c + 1) / 2;
        return float(c & 1 ? -out : out) * kStartWidth / float(kChannels / 2);
    }
    static constexpr float kStartWidth = .8f;

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
    bool Down(int i) const { return (held_ >> i) & 1; }
    uint64_t held_ = 0, pressed_ = 0;
    static constexpr int kMostDown = 12; // more switches down at once: a misread
    uint32_t misreads_ = 0;              // ... so many since power-on
    UsbView usb_view_ = {};

    float sr_ = 48000.f;
    float preset_gain_[kNumMalletPresets] = {};
    Channel ch_[kChannels];
    Slot slot_[kVoices];
    Pan pan_[kChannels];         // each channel's pan, as heard
    float bus_[kChannels][64];   // each channel's sum in this block (blocks of up to 64)
    bool busy_[kChannels] = {};  // ... has a voice in it
    void Place(int c, bool snap) {
        pan_[c].Set(ch_[c].pan, size_);
        if (snap) pan_[c].Snap();
    }
    void PlaceAll(bool snap) { for (int c = 0; c < kChannels; ++c) Place(c, snap); }
    bool dirty_[kChannels] = {};
    uint32_t clock_ = 0;
    int sel_ = 0;
    float led_time_ = 0.f;
    float swell_[kChannels] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    uint32_t rng_ = 12345;
    bool all_ = false, show_load_ = false;
    float room_ = .5f, volume_ = .4f, load_ = 0.f; // the volume at power-on: 6 dB under the old .8
    uint32_t shed_ = 0; // voices the load guard has cut
    float size_ = 1.f;  // the width: 0 all in the middle, 1 as panned, 2 pushed further out
    float gain_ = 1.f;  // the ensemble's level, on every channel's own (0..2)

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
        meter_phase_ = false;
    }
    bool wheel_turned_ = false; // the wheel turned while pushed (a kick), so its release is not 'home'
    bool meter_phase_ = false; // the meter is a player's place against its ghost (drift) ...
    int meter_ch_ = 0;         // ... this player's
    float key_flash_[25] = {}, note_flash_[kChannels] = {};
    int step_clicks_ = 0, step_id_ = 0, step_idle_ = 0, oct_show_ = 0;
    int reboot_hold_ = 0;

    Loop loop_[kChannels];
    History hist_[kChannels];
    uint32_t now_ = 0;      // samples since start
    float l0_ = 96000.f;    // L0 in samples (2 s)
    float l0_pos_ = 0.f, l0_flash_ = 0.f, ch_flash_[kChannels] = {};
    // The click before the first figure: samples into it, -1 silent. A sine at
    // kClickHz falling by e every kClickDecay, kClickSeconds long.
    int click_ = -1;
    static constexpr float kClickHz = 2000.f, kClickDecay = .006f, kClickSeconds = .04f, kClickLevel = .25f;
    uint32_t l0_beats_ = 0; // L0 beats so far
    double kept_beat_[kChannels] = {}; // when each figure was kept (Beats), and where its loop was
    float kept_pos_[kChannels] = {};
    double home_[kChannels] = {};      // drift: the beat each loop's place started on (see Anchor)
    float detune_[kChannels] = {};     // ... each player's own speed, -0.05 .. 0.05
    bool glide_[kChannels] = {};       // ... gliding home
    float pull_ = kStartPull;          // ... how strongly everyone is pulled to their place
    bool running_ = true;
    bool align_ = true; // Options.h: Align To L0
    bool loop_down_ = false, loop_used_ = false, loop_chompi_ = false;
    bool play_down_ = false, play_used_ = false;
    bool muted_[kChannels] = {};
    uint32_t listen_ = 0;     // channels listened to (play + white key held), a bit each
    int white_down_[kChannels] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}; // blocks since pressed with play, -1: not
    static constexpr int kListenBlocks = 250; // play + white held this long: listen; shorter: a tap, mute
    bool sound_view_ = false; // the selected channel's sound on the white keys
    int view_set_ = 0;        // the set of instruments shown there
    int tap_key_ = -1;        // the white key last tapped with CHOMPI held
    bool tap_again_ = false;  // ... and it was tapped before: the sounds on letting go
    float gate_[kChannels] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    int ens_ = kRatioOne, pending_ens_ = -1;
    int pending_[kChannels] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    int loop_hold_ = 0, loop_ch_ = 0;
    Versions* ver_ = nullptr;
    uint32_t loop_t_ = 0, loop_since_ = 0; // when loop went down; the channel's since before it
    int loop_ratio_ = kRatioOne;           // the selected channel's length then
    float loop_span_ = 0.f;                // how far back the playing reached then, in samples
    bool loop_kept_ = false;               // this hold has kept a figure
    bool len_show_ = false;                // the lengths on the black keys (loop held)
    bool chose_ = false;                   // a player was chosen in this CHOMPI hold
    float kept_flash_ = 0.f, nokeep_ = 0.f, sel_glow_ = 0.f; // LED feedback, seconds / brightness
    static constexpr int kLenShowBlocks = 150; // loop held this long before the lengths show
    static constexpr int kTapBlocks = 400;     // a loop press shorter than this is a tap
    static constexpr float kNoKeepSeconds = .6f, kSelGlowSeconds = .5f;
    bool vshow_ = false, vshow_all_ = false; // stepping through versions: shown on the black keys
    bool shift_show_ = false, shift_saved_ = false; // sliding the figure (loop held + wheel); saved once per hold
    static constexpr int kShiftSteps = 64;
    static constexpr int kStepVersions = 100; // Steps() id of the versions (the octave's: the channel)
    static constexpr int kClicksPerVersion = 2;
    // The library: the figure each player plays (its number, -1: none from the
    // library) and the one it moves on to at its next loop start (-1: none).
    int lib_at_[kChannels] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    int lib_next_[kChannels] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    int lib_show_ = 0, lib_show_fig_ = -1; // a figure's notes on the keys, so many blocks more
    static constexpr int kStepLibrary = 101; // Steps() id of the library
    bool reboot_armed_ = false, reboot_now_ = false;
};

} // namespace meter
