#pragma once
// A voice of the pool: the mallet, or one of the sound study's instruments
// (study/instruments.h), whichever the note's preset asks for. A study voice
// is built in the slot's own memory when the slot changes instrument, its
// buffers in the slot's piece of SDRAM, so nothing is allocated while playing.

#include <new>

#include "../dsp/Mallet.h"
#include "../dsp/Presets.h"
#include "../study/instruments.h"

namespace meter {

template <class... T> constexpr size_t MaxSize() { size_t m = 0; ((m = sizeof(T) > m ? sizeof(T) : m), ...); return m; }

class AnyVoice {
public:
    // The most any study voice keeps in delay lines (the pulse piano, 24 KB).
    static constexpr int kArenaFloats = 8192;

    void Init(float sample_rate, uint32_t seed, float* arena) {
        sr_ = sample_rate;
        seed_ = seed;
        arena_ = arena;
        m_.Init(sample_rate, seed);
    }

    // The instruments that sound on (breath, voice, bow, tape) and the piano:
    // struck again, they would start over from silence, a click, or (the
    // piano's strings) pile up; so a repeat goes to another voice and this one
    // fades out under it (Fade).
    static bool Sustained(int engine) {
        return engine == kEngSiku || engine == kEngVoices || engine == kEngBowed || engine == kEngTape || engine == kEngPiano;
    }
    int Engine() const { return engine_; }

    void Strike(float freq, const MalletPreset& preset, const MalletParams& p, float velocity) {
        fade_ = 1.f;
        fade_step_ = 0.f;
        if (preset.engine != engine_) Build(preset.engine);
        if (engine_ == kEngMallet) { m_.Strike(freq, p, velocity); return; }
        sd::Knobs k;
        k.strike = p.strike;
        k.bright = p.brightness;
        k.decay = p.decay;
        k.shimmer = p.shimmer;
        k.feel = p.variation;
        k.prep = p.buzz;
        s_->Strike(freq, velocity, k);
    }
    // The knobs reach a ringing mallet at once; a study voice takes them with its next note.
    void SetParams(const MalletParams& p) { if (engine_ == kEngMallet) m_.SetParams(p); }
    float Process() {
        float y = engine_ == kEngMallet ? m_.Process() : s_->Process();
        if (fade_step_ > 0.f) {
            fade_ -= fade_step_;
            if (fade_ <= 0.f) { fade_ = 0.f; fade_step_ = 0.f; Stop(); }
            y *= fade_;
        }
        return y;
    }
    // Out within `seconds`, smoothly.
    void Fade(float seconds) { if (Active()) fade_step_ = 1.f / std::max(seconds * sr_, 1.f); }
    bool Fading() const { return fade_step_ > 0.f; }
    bool Active() const { return engine_ == kEngMallet ? m_.Active() : s_->active; }
    void Stop() { if (engine_ == kEngMallet) m_.Stop(); else s_->active = false; }

private:
    void Build(int engine) {
        if (s_) { s_->~Voice(); s_ = nullptr; }
        m_.Stop();
        engine_ = engine;
        if (engine == kEngMallet) return;
        void* at = mem_;
        switch (engine) {
            case kEngGangsa: s_ = new (at) sd::Gangsa(); break;
            case kEngMbira: s_ = new (at) sd::Mbira(); break;
            case kEngAmadinda: s_ = new (at) sd::Amadinda(); break;
            case kEngSiku: s_ = new (at) sd::Siku(); break;
            case kEngKora: s_ = new (at) sd::Kora(); break;
            case kEngVoices: s_ = new (at) sd::Voices(); break;
            case kEngHands: s_ = new (at) sd::Hands(); break;
            case kEngBowed: s_ = new (at) sd::Bowed(); break;
            case kEngBowl: s_ = new (at) sd::Bowl(); break;
            case kEngPiano: s_ = new (at) sd::Piano(); break;
            case kEngHang: s_ = new (at) sd::Hang(); break;
            default: s_ = new (at) sd::Tape(); break;
        }
        sd::Arena a{arena_, size_t(kArenaFloats)};
        sd::g_arena = &a;
        s_->Init(sr_, seed_);
        sd::g_arena = nullptr;
    }

    static constexpr size_t kMem = MaxSize<sd::Gangsa, sd::Mbira, sd::Amadinda, sd::Siku, sd::Kora, sd::Voices, sd::Hands,
                                           sd::Bowed, sd::Bowl, sd::Piano, sd::Hang, sd::Tape>();
    MalletVoice m_;
    sd::Voice* s_ = nullptr;
    int engine_ = kEngMallet;
    float sr_ = 48000.f;
    uint32_t seed_ = 1;
    float* arena_ = nullptr;
    float fade_ = 1.f, fade_step_ = 0.f;
    alignas(8) unsigned char mem_[kMem];
};

} // namespace meter
